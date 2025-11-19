/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mhummel <mhummel@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:36 by mhummel           #+#    #+#             */
/*   Updated: 2025/11/19 10:24:58 by mhummel          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
#include <unistd.h>
#include <limits.h>

// globals
static Config g_cfg;
static std::vector<pollfd>     fds;
static std::unordered_set<int> listener_fds;
static std::vector<Client>     clients;
static std::unordered_map<int /*port*/, std::vector<size_t> /*server indices*/> servers_by_port;
static std::unordered_map<int /*lfd*/,  int /*port*/>      port_by_listener_fd;

int make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void send_error_and_close(size_t i, int code, const std::string& text,
                                 std::vector<pollfd>& fds, std::vector<Client>& clients)
{
    Client& c = clients[i];
    std::string body = std::to_string(code) + " " + text + "\n";
    c.tx = "HTTP/1.1 " + std::to_string(code) + " " + text + "\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n\r\n" + body;
    fds[i].events |= POLLOUT;
}

inline void err400(size_t i, std::vector<pollfd>& fds, std::vector<Client>& clients){ send_error_and_close(i,400,"Bad Request",fds,clients); }
inline void err413(size_t i, std::vector<pollfd>& fds, std::vector<Client>& clients){ send_error_and_close(i,413,"Payload Too Large",fds,clients); }
inline void err505(size_t i, std::vector<pollfd>& fds, std::vector<Client>& clients){ send_error_and_close(i,505,"HTTP Version Not Supported",fds,clients); }

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

    if (::bind(s, (sockaddr*)&a, sizeof(a)) < 0) { perror("bind"); ::close(s); return -1; }
    if (::listen(s, 128) < 0)                     { perror("listen"); ::close(s); return -1; }
    if (make_nonblocking(s) < 0)                  { perror("fcntl");  ::close(s); return -1; }

    pollfd p{}; p.fd = s; p.events = POLLIN; p.revents = 0;
    //fds debug print
    // std::cout << "Added listener fd=" << s << " on port " << port << "\n";
    fds.push_back(p);
    clients.push_back(Client{}); // Dummy, hält Index-Sync
    listener_fds.insert(s);

    std::cout << "Listening on 0.0.0.0:" << port << "\n";
    return s;
}












int main(int argc, char** argv)
{
    // === 1. AUTOMATISCHER CONFIG-PFAD ===
    const char* cfg_path = "./config/webserv.conf";
    if (argc > 1) {
        cfg_path = argv[1];  // Optional: ./webserv my.conf
    }

    // === 2. CONFIG LADEN MIT FALLBACK ===
    try {
        g_cfg.parse_c(cfg_path);
        std::cout << "Config geladen: " << cfg_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Config-Fehler (" << cfg_path << "): " << e.what() << "\n";
        std::cerr << "→ Starte mit Default-Server auf 127.0.0.1:8080\n";

        // --- DEFAULT-SERVER MANUELL ANLEGEN ---
        ServerConfig defaultServer;
        defaultServer.listen_host = "127.0.0.1";
        defaultServer.listen_port = 8080;
        defaultServer.server_name = "default";
        defaultServer.client_max_body_size = 1048576;  // 1MB

        LocationConfig defaultLoc;
        defaultLoc.path = "/";
        defaultLoc.root = "./root/html";
        defaultLoc.index = "index.html";
        defaultLoc.autoindex = true;
        defaultLoc.methods = {"GET", "POST", "DELETE"};

        defaultServer.locations.push_back(defaultLoc);
        g_cfg.servers.push_back(defaultServer);
    }

    // === 3. DEFAULTS FÜR ALLE SERVER/LOCATIONS SETZEN ===
    for (auto& server : g_cfg.servers) {
        if (server.listen_port == 0) server.listen_port = 80;
        if (server.client_max_body_size == 0) server.client_max_body_size = 1048576;
        if (server.error_pages.empty()) server.error_pages = g_cfg.default_error_pages;

        for (auto& loc : server.locations) {
            if (loc.index.empty()) loc.index = "index.html";
            if (loc.methods.empty()) loc.methods = {"GET", "POST", "DELETE"};
            if (loc.error_pages.empty()) loc.error_pages = server.error_pages;
            if (!loc.autoindex) loc.autoindex = false;
        }
    }

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
            std::cout << "Listening on *:" << port << " (lfd=" << lfd << ")\n";
        }
        servers_by_port[port].push_back(s);
    }

    const long IDLE_MS = 1500000; // timeout zeit
    char buf[4096];
    std::cout << "Echo server with write-buffer on port 8080...\n";

    for (;;)
	{
        // zeit setup
        using clock_t = std::chrono::steady_clock;
        using ms      = std::chrono::milliseconds;

        long now_ms = std::chrono::duration_cast<ms>(clock_t::now().time_since_epoch()).count();
        for (size_t i = 1; i < fds.size(); ++i) {
            if (listener_fds.count(fds[i].fd)) continue;
            if (now_ms - clients[i].last_active_ms > IDLE_MS) {
                std::cerr << "[TIMEOUT] fd=" << fds[i].fd
                        << " idle=" << (now_ms - clients[i].last_active_ms) << "ms\n";
                ::close(fds[i].fd);
                fds.erase(fds.begin()+i);
                clients.erase(clients.begin()+i);
                --i;
            }
        }

        //poll
        int ready = poll(&fds[0], fds.size(), 1000);
        if (ready < 0) { if (errno==EINTR) continue; perror("poll"); break; }

        for (size_t i = 0; i < fds.size(); ++i)
		{
            if (fds[i].revents == 0) continue;

            int fd = fds[i].fd;
            bool is_listener = (listener_fds.find(fd) != listener_fds.end());

            if (is_listener)
			{
                for (;;)
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

                    std::cout << "New client " << cfd << " via port " << port
                              << " -> server#" << c.server_idx << "\n";
                }
                continue;
            }

            if (fds[i].revents & (POLLHUP | POLLERR | POLLNVAL))
			{
                close(fds[i].fd);
                fds.erase(fds.begin() + i);
                clients.erase(clients.begin() + i);
                --i;
                continue;
            }

            // Lesen
            if (fds[i].revents & POLLIN)
			{
                for (;;)
				{
                    ssize_t n = ::read(fds[i].fd, buf, sizeof(buf));
                    //DEBUG read check
                    // std::cout << "========= Read returned buf=" << buf << " =========" << std::endl;
                    if (n > 0)
					{
                        Client &c = clients[i];
                        c.last_active_ms = now_ms;
                        c.rx.append(buf, n);
                        //DEBG c.rx
                        // std::cout << "----- c.rx " <<c.rx << " -----" << std::endl;

// ------ hier Leo sein Zeug rein
// ------ aus raw string alles rausgeholt und in Request struct
// ------ ab hier

                        // Request wirklich komplett oder chunked??             ---FIX
                        // Request req;
                        // if (c.rx.find("\r\n\r\n") != std::string::npos)
                        // {
                        //     req = RequestParser().parse(c.rx);
                        //     std::cout << "Body :" << req.body << ":\n";
                        //     c.state = RxState::READY; // Für dieses Beispiel direkt READY setzen
                        //     c.target = req.path;
                        // }

                        size_t headerEnd = c.rx.find("\r\n\r\n");
                        if (headerEnd == std::string::npos)
                            continue; // Header noch nicht komplett, weiter lesen

                        std::string headers = c.rx.substr(0, headerEnd + 4);
                        size_t clPos = headers.find("Content-Length:");
                        size_t contentLength = 0;
                        if (clPos != std::string::npos)
                        {
                            clPos += std::string("Content-Length:").length();
                            // Whitespace überspringen
                            while (clPos < headers.size() && (headers[clPos] == ' ' || headers[clPos] == '\t'))
                                ++clPos;
                            size_t clEnd = headers.find("\r\n", clPos);
                            std::string clStr = headers.substr(clPos, clEnd - clPos);
                            contentLength = std::atoi(clStr.c_str());
                        }

                        size_t totalNeeded = headerEnd + 4 + contentLength;
                        if (c.rx.size() < totalNeeded) {
                            // Wir haben den kompletten Header, aber noch nicht den ganzen Body
                            // -> noch nichts parsen, weiter recv() machen
                            continue;
                        }

                        std::string fullRequest = c.rx.substr(0, totalNeeded);

                        Request req = RequestParser().parse(fullRequest);
                        std::cout << "Body :" << req.body << ":\n";

                        c.state = RxState::READY;
                        c.target = req.path;

                        // Verbrauchte Bytes aus dem Buffer entfernen (wichtig bei keep-alive!)
                        c.rx.erase(0, totalNeeded);



// ------ leos part ersetzt bis hier


                        // Limits an finalen Server anpassen (z. B. 413 später korrekt)
                        const ServerConfig& sc = g_cfg.servers[c.server_idx];
                        c.max_body_bytes = sc.client_max_body_size;

                        // ---- Location bestimmen (Longest Prefix Match) ----
                        auto resolve_location = [](const ServerConfig& sc, const std::string& path)->const LocationConfig&
						{
                            size_t best = 0, best_len = 0;
                            for (size_t i = 0; i < sc.locations.size(); ++i) {
                                const std::string& p = sc.locations[i].path;
                                if (!p.empty() && path.compare(0, p.size(), p) == 0 && p.size() > best_len) {
                                    best = i; best_len = p.size();
                                }
                            }
                            if (best_len == 0) {
                                for (size_t i = 0; i < sc.locations.size(); ++i)
                                    if (sc.locations[i].path == "/") return sc.locations[i];
                                return sc.locations.front();
                            }
                            return sc.locations[best];
                        };

                        const LocationConfig& lc = resolve_location(sc, c.target);
                        std::cout << "lc root " << lc.root << std::endl;
                        c.last_active_ms = now_ms;

                        if (c.state == RxState::READY && c.tx.empty())
                        {
                            req.conn_fd   = fds[i].fd;

                            ResponseHandler handler;
                            printf("method: %s, path: %s\n", req.method.c_str(), req.path.c_str());
                            Response res = handler.handleRequest(req, lc);
                            //CoreResponse resp =  RequestParser.parse(req); // <- später echtes Modul deines Kumpels

                            c.keep_alive = res.keep_alive; // Server-Core entscheidet final über close/keep-alive
                            c.tx         = res.toString();
                            fds[i].events |= POLLOUT;
                        }


// alles mehr oder weniger leo

                        continue; // weiter lesen, falls Kernel noch mehr hat
                    }
					else if (n == 0)
					{
                        ::close(fds[i].fd);
                        fds.erase(fds.begin()+i);
                        clients.erase(clients.begin()+i);
                        --i; break;
                    }
					else
					{
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("read");
                        ::close(fds[i].fd);
                        fds.erase(fds.begin()+i);
                        clients.erase(clients.begin()+i);
                        --i; break;
                    }
                }
            }

            // Schreiben
            if (fds[i].revents & POLLOUT)
			{
                Client &c = clients[i];
                while (!c.tx.empty())
				{
                    ssize_t m = write(fds[i].fd, c.tx.data(), c.tx.size());
                    if (m > 0) { c.tx.erase(0, m); c.last_active_ms = now_ms; continue; }
                    if (m < 0 && (errno==EAGAIN || errno==EWOULDBLOCK)) break;
                    if (m < 0) { perror("write"); break; }
                }
                if (c.tx.empty())
				{
                    if (c.keep_alive)
					{
                        reset_for_next_request(c);
                        fds[i].events &= ~POLLOUT;          // zurück auf nur lesen
                        // Verbindung offen lassen
                    }
					else
					{
                        ::close(fds[i].fd);
                        fds.erase(fds.begin()+i);
                        clients.erase(clients.begin()+i);
                        --i;
                    }
                }
            }
        }
    }

    for (auto &p : fds) ::close(p.fd);
    return 0;
}
