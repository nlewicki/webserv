/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: nicolewicki <nicolewicki@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:36 by mhummel           #+#    #+#             */
/*   Updated: 2025/12/08 21:21:00 by nicolewicki      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
#include <unistd.h>
#include <limits.h>

// globals
static std::vector<pollfd>     fds;
static std::unordered_set<int> listener_fds;
static std::vector<Client>     clients;
static std::unordered_map<int /*port*/, std::vector<size_t> /*server indices*/> servers_by_port;
static std::unordered_map<int /*lfd*/,  int /*port*/>      port_by_listener_fd;

static const size_t MAX_HEADER_BUFFER = 16 * 1024; // 16 KiB für Header


int make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void reset_for_next_request(Client& c)
{
    c.tx.clear();
    c.rx.clear();
    c.state = RxState::READING_HEADERS;
    c.header_done = false;
    c.is_chunked = false;
    c.content_len = 0;
    c.body_rcvd = 0;
    c.ch_state = Client::ChunkState::SIZE;
    c.ch_need  = 0;
}

static int add_listener(uint16_t port)
{
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return -1; }
    int yes = 1;
    if (::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("setsockopt"); ::close(s); return -1;
    }

    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port        = htons(port);

    if (::bind(s, (sockaddr*)&a, sizeof(a)) < 0)
    {
        perror("bind");
        ::close(s);
        return -1;
    }

    if (::listen(s, 128) < 0)
    {
        perror("listen");
        ::close(s);
        return -1;
    }

    if (make_nonblocking(s) < 0)
    {
        perror("fcntl");
        ::close(s);
        return -1;
    }

    pollfd p{}; p.fd = s; p.events = POLLIN; p.revents = 0;
    #ifdef DEBUG
    std::cout << "Added listener fd=" << s << " on port " << port << "\n";
    #endif
    fds.push_back(p);
    clients.push_back(Client{}); // Dummy, hält Index-Sync
    listener_fds.insert(s);

    std::cout << "Listening on 0.0.0.0:" << port << "\n";
    return s;
}


int webserv(int argc, char* argv[])
{
    Server server;
    return server.run(argc, argv);
}

static void setupBuiltinDefaultConfig()
{
	ServerConfig srv;

	srv.listen_host = "127.0.0.1";
	srv.listen_port = 8080;
	srv.server_name = "localhost";
	srv.client_max_body_size = 10 * 1024 * 1024;  // 10M – wie in conf

	// Globale Direktive
	g_cfg.default_error_pages[404] = "/errors/404.html";
	g_cfg.default_client_max_body_size = srv.client_max_body_size;

	// === location / ===
	{
		LocationConfig loc;
		loc.path = "/";
		loc.root = "./root/html";
		loc.index = "index.html";
		loc.autoindex = true;
		loc.methods = {"GET", "POST"};
		srv.locations.push_back(loc);
	}

	// === location /data ===
	{
		LocationConfig loc;
		loc.path = "/root/data";
		loc.root = "./root/data";
		loc.autoindex = true;
		loc.methods = {"GET", "POST"};
		srv.locations.push_back(loc);
	}

	// === location /root/cgi-bin ===
	{
		LocationConfig loc;
		loc.path = "/root/cgi-bin";
		loc.root = "./root/cgi-bin";
		loc.cgi[".py"]  = "/usr/bin/python3";
		loc.cgi[".php"] = "/usr/bin/php-cgi";
		loc.methods = {"GET", "POST"};
		srv.locations.push_back(loc);
	}

	// Alles übernehmen
	g_cfg.servers.clear();
	g_cfg.servers.push_back(srv);
}

void Server::loadConfig(int argc, char* argv[])
{
	if (argc > 2) {
		std::cerr << "Usage: " << argv[0] << " [config_file.conf]\n";
		exit(1);
	}

	std::string configPath;

	if (argc == 2) {
		configPath = argv[1];

		// Nur .conf erlauben
		if (configPath.size() < 5 || configPath.substr(configPath.size() - 5) != ".conf") {
			std::cerr << "Error: Config file must have .conf extension!\n";
			exit(1);
		}
	} else {
		configPath = "./config/webserv.conf";
		std::cout << "No config file specified → trying default: " << configPath << "\n";
	}

	// Versuch, die Config zu laden
	try {
		g_cfg.parse_c(configPath);
		std::cout << "Config successfully loaded: " << configPath << "\n";
		return;
	}
	catch (const std::exception& e) {
		std::cerr << "Failed to load config '" << configPath << "': " << e.what() << "\n";
		std::cerr << "→ Starting with built-in default configuration\n";
	}

	// Fallback: manueller, 100 % aktueller Default-Server
	setupBuiltinDefaultConfig();
	std::cout << "Built-in default server activated on 127.0.0.1:8080\n";
}

void Server::setupListeners()
{
    // === 4. LISTENER AUS CONFIG STARTEN ===
    std::unordered_map<int, int> lfd_by_port;

    for (size_t s = 0; s < g_cfg.servers.size(); ++s)
    {
        const ServerConfig& sc = g_cfg.servers[s];
        int port = sc.listen_port;

        if (lfd_by_port.find(port) == lfd_by_port.end())
        {
            int lfd = add_listener(port);
            lfd_by_port[port] = lfd;
            port_by_listener_fd[lfd] = port;
            #ifdef DEBUG
            std::cout << "Listening on *:" << port << " (lfd=" << lfd << ")\n";
            #endif
        }
        servers_by_port[port].push_back(s);
    }
}

void Server::closeClient(size_t &i)
{
    ::close(fds[i].fd);
    fds.erase(fds.begin() + i);
    clients.erase(clients.begin() + i);
    --i;
}

void Server::handleTimeouts(long now_ms, long IDLE_MS)
{
    for (size_t i = 1; i < fds.size(); ++i)
    {
            if (listener_fds.count(fds[i].fd)) continue;
            if (now_ms - clients[i].last_active_ms > IDLE_MS)
            {
                std::cerr << "[TIMEOUT] fd=" << fds[i].fd
                        << " idle=" << (now_ms - clients[i].last_active_ms) << "ms\n";
                closeClient(i);
            }
        }
}

void Server::handleListenerEvent(size_t index, long now_ms)
{
    int fd = fds[index].fd;

    while (1)
    {
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0)
        {
            if (errno==EAGAIN || errno==EWOULDBLOCK) break;
            perror("accept"); break;
        }
        make_nonblocking(cfd);
        pollfd cp{}; cp.fd = cfd; cp.events = POLLIN; cp.revents = 0;
        fds.push_back(cp);

        Client c;
        c.last_active_ms = now_ms;


        int port = port_by_listener_fd[fd];
        c.listen_port = port;

        // Default-Server (falls mehrere vHosts auf gleichem Port – später durch Host-Header präzisieren)
        c.server_idx = servers_by_port[port].front();

        // Body-Limit erstmal mit Server-Default belegen (wird nach Host-Match evtl. noch aktualisiert)
        const ServerConfig& sc0 = g_cfg.servers[c.server_idx];
        c.max_body_bytes = sc0.client_max_body_size;
        clients.push_back(c);

        std::cout << "New client " << cfd << " via port " << port << " -> server#" << c.server_idx << "\n";
    }
}

static const LocationConfig& resolve_location(const ServerConfig& sc, const std::string& path)
{
    size_t best = 0, best_len = 0;
    for (size_t j = 0; j < sc.locations.size(); ++j)
    {
        const std::string& p = sc.locations[j].path;
        if (!p.empty() && path.compare(0, p.size(), p) == 0 && p.size() > best_len)
        {
            best = j;
            best_len = p.size();
        }
    }
    if (best_len == 0)
    {
        for (size_t j = 0; j < sc.locations.size(); ++j)
            if (sc.locations[j].path == "/") return sc.locations[j];
        return sc.locations.front();
    }
    return sc.locations[best];
};

bool Server::handleClientRead(size_t &i, long now_ms, char* buf, size_t buf_size)
{
    ssize_t n = ::read(fds[i].fd, buf, buf_size);

    if (n > 0)
    {
        Client &c = clients[i];
        c.last_active_ms = now_ms;
        c.rx.append(buf, n);

        // 0) Header-Buffer-Guard
        if (!c.header_done && c.rx.size() > MAX_HEADER_BUFFER)
        {
            ResponseHandler handler;
            Response res = handler.makeHtmlResponse(
                431,
                "<h1>431 Request Header Fields Too Large</h1>"
            );
            c.tx = res.toString();

            // Ab jetzt nur noch schreiben, nichts mehr lesen
            fds[i].events &= ~POLLIN;
            fds[i].events |= POLLOUT;

            return true;
        }

        // 1) Header-Ende suchen
        size_t headerEnd = c.rx.find("\r\n\r\n");
        if (headerEnd == std::string::npos)
            return true; // Header noch nicht vollständig

        c.header_done = true;

        std::string headers = c.rx.substr(0, headerEnd + 4);

        // 2) Kopf (Request Line + Header) parsen
        RequestParser parser;
        Request req;
        if (!parser.parseHeaders(headers, req))
        {
            ResponseHandler handler;
            Response res = handler.makeHtmlResponse(400, "<h1>400 Bad Request</h1>");
            c.tx = res.toString();
            fds[i].events &= ~POLLIN;
            fds[i].events |= POLLOUT;
            return true;
        }

        // 3) vHost bestimmen
        int port = c.listen_port;
        size_t server_idx = servers_by_port[port].front();
        std::string host = req.headers["Host"];

        if (!host.empty() && servers_by_port.count(port))
        {
            size_t colon = host.find(':');
            if (colon != std::string::npos) host = host.substr(0, colon);

            for (size_t idx : servers_by_port[port]) {
                if (g_cfg.servers[idx].server_name == host) {
                    server_idx = idx;
                    break;
                }
            }
        }

        c.server_idx = server_idx;
        const ServerConfig& sc = g_cfg.servers[server_idx];

        // 4) Location bestimmen
        const LocationConfig& lc = resolve_location(sc, req.path);

        // 5) Early Check für Content-Length (falls nicht chunked)
        bool isChunked = req.headers.count("Transfer-Encoding") &&
                         req.headers["Transfer-Encoding"] == "chunked";

        size_t contentLength = 0;
        size_t maxBody = (lc.client_max_body_size > 0)
                         ? lc.client_max_body_size
                         : sc.client_max_body_size;

        if (!isChunked && req.headers.count("Content-Length")) {
            try {
                contentLength = std::stoul(req.headers["Content-Length"]);
            } catch (...) {
                contentLength = 0;
            }

            if (maxBody > 0 && contentLength > maxBody) {
                // Direkt 413 senden, Body ignorieren
                ResponseHandler handler;
                Response res = handler.makeHtmlResponse(
                    413,
                    "<h1>413 Payload Too Large</h1>"
                );
                c.tx = res.toString();
                c.keep_alive = false;            // nach Fehler Verbindung schließen
                fds[i].events &= ~POLLIN;
                fds[i].events |= POLLOUT;
                return true;
            }
        }

        // 6) Gesamten Request-Body warten
        size_t totalNeeded = headerEnd + 4; // Header + Leerzeile
        size_t bodyStart = headerEnd + 4;

        if (isChunked) {
            size_t endMarker = c.rx.find("0\r\n\r\n", bodyStart);
            if (endMarker == std::string::npos)
                return true; // Chunked Body noch nicht komplett
            totalNeeded = endMarker + 5; // inkl. "0\r\n\r\n"
        } else {
            totalNeeded += contentLength;
            if (c.rx.size() < totalNeeded)
                return true; // Body noch nicht komplett da
        }

        // 7) Vollständigen Request parsen (inkl. Body)
        std::string fullRequest = c.rx.substr(0, totalNeeded);
        std::istringstream stream(fullRequest);

        // Skip Headers (bereits geparst)
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                break;
        }

        // 8) Body mit den Parser-Funktionen parsen
        if (!parser.parseBody(stream, req, lc, sc)) {
            int status = req.error ? req.error : 400;

            ResponseHandler handler;
            Response res = handler.makeHtmlResponse(
                status,
                "<h1>" + std::to_string(status) + " Error</h1>"
            );

            c.keep_alive = false;   // bei kaputtem Request ruhig Verbindung schließen
            c.tx         = res.toString();
            fds[i].events &= ~POLLIN;
            fds[i].events |= POLLOUT;

            // Verbrauchte Bytes entfernen
            c.rx.erase(0, totalNeeded);

            return true;
        }

        #ifdef DEBUG
        std::cout << "[SERVER] Parsed request body: '" << req.body << "'" << std::endl;
        std::cout << "[SERVER] Body size: " << req.body.size() << std::endl;
        #endif

        c.state  = RxState::READY;
        c.target = req.path;

        // Verbrauchte Bytes entfernen
        c.rx.erase(0, totalNeeded);

        // 9) Response generieren
        if (c.state == RxState::READY && c.tx.empty())
        {
            req.conn_fd = fds[i].fd;

            ResponseHandler handler;
            Response res = handler.handleRequest(req, lc, sc);  // + sc (serverConfig)

            c.keep_alive = res.keep_alive;
            c.tx         = res.toString();
            fds[i].events |= POLLOUT;
        }
        return true;
    }
    else if (n == 0)
    {
        closeClient(i);
        return false;
    }
    else
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        perror("read");
        closeClient(i);
        return false;
    }
}



bool Server::handleClientWrite(size_t &i, long now_ms)
{
    Client &c = clients[i];

    if (c.tx.empty())
        return true;

    ssize_t m = write(fds[i].fd, c.tx.data(), c.tx.size());

    if (m > 0)
    {
        c.tx.erase(0, m);
        c.last_active_ms = now_ms;
    }
    else if (m < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return true;
    else if (m < 0)
    {
        perror("write");
        closeClient(i);
        return false;
    }
    else
    { // m == 0
        // Sehr ungewöhnlich, aber sauber behandeln – am besten Verbindung schließen
        closeClient(i);
        return false;
    }

    if (c.tx.empty())
    {
        if (c.keep_alive)
        {
            reset_for_next_request(c);
            fds[i].events &= ~POLLOUT;  // nur noch lesen
            return true;
        }
        else
        {
            closeClient(i);
            return false;
        }
    }

    return true;
}


int Server::run(int argc, char* argv[])
{
    loadConfig(argc, argv);

    // === 3. DEFAULTS FÜR ALLE SERVER/LOCATIONS SETZEN ===
    for (auto& server : g_cfg.servers)
    {
        if (server.listen_port == 0)
            server.listen_port = 80;
        if (server.client_max_body_size == 0)
            server.client_max_body_size = g_cfg.default_client_max_body_size;
        if (server.error_pages.empty())
            server.error_pages = g_cfg.default_error_pages;

        for (auto& loc : server.locations) {
            if (loc.index.empty())
                loc.index = "index.html";
            if (loc.methods.empty())
                loc.methods = {"GET", "POST", "DELETE"};
            if (loc.error_pages.empty())
                loc.error_pages = server.error_pages;
            if (!loc.autoindex)
                loc.autoindex = false;
        }
    }

    setupListeners();

    const long IDLE_MS = 150000; // timeout zeit
    char buf[4096];
    #ifdef DEBUG
    std::cout << "Echo server with write-buffer on port 8080...\n";
    #endif

    while (1)
	{
        // zeit setup
        using clock_t = std::chrono::steady_clock;
        using ms      = std::chrono::milliseconds;

        long now_ms = std::chrono::duration_cast<ms>(clock_t::now().time_since_epoch()).count();
        handleTimeouts(now_ms, IDLE_MS);

        //poll
        int ready = poll(&fds[0], fds.size(), 1000);
        if (ready < 0)
        {
            if (errno==EINTR)
                continue;
            perror("poll");
            break;
        }

        //Events abarbeiten
        for (size_t i = 0; i < fds.size(); ++i)
		{
            if (fds[i].revents == 0)
                continue;

            int fd = fds[i].fd;
            bool is_listener = (listener_fds.find(fd) != listener_fds.end());

            if (is_listener)
			{
                handleListenerEvent(i, now_ms);
                continue;
            }

            if (fds[i].revents & (POLLHUP | POLLERR | POLLNVAL))
			{
                closeClient(i);
                continue;
            }
            // Lesen
            if (fds[i].revents & POLLIN)
            {

                if (!handleClientRead(i, now_ms, buf, sizeof(buf)))
                    continue;
            }
            // Schreiben
            if (fds[i].revents & POLLOUT)
			{
                if (!handleClientWrite(i, now_ms))
                    continue;
            }
        }
    }

    for (auto &p : fds)
        ::close(p.fd);
    return 0;
}
