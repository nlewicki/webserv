/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: nicolewicki <nicolewicki@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:36 by mhummel           #+#    #+#             */
/*   Updated: 2025/10/27 10:07:41 by nicolewicki      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"

int make_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static std::vector<pollfd>     fds;
static std::unordered_set<int> listener_fds;
static std::vector<Client>     clients;

static Config g_cfg;

// Port-/Server-Mappings
static std::unordered_map<int /*port*/, std::vector<size_t> /*server indices*/> servers_by_port;
static std::unordered_map<int /*lfd*/,  int /*port*/>      port_by_listener_fd;

// static std::string lcase(std::string s){ for(char&c: s) c = std::tolower((unsigned char)c); return s; }
// static std::string trim(const std::string& s){
//     const char* ws = " \t\r\n";
//     auto a = s.find_first_not_of(ws); if (a==std::string::npos) return "";
//     auto b = s.find_last_not_of(ws);  return s.substr(a, b-a+1);
// }

// static bool parse_head_min(const std::string& head, HeadInfo& out){
//     // Startzeile
//     auto eol = head.find("\r\n"); if (eol==std::string::npos) return false;
//     std::istringstream iss(head.substr(0, eol));
//     if (!(iss >> out.method >> out.target >> out.version)) return false;
//     // Header
//     std::unordered_map<std::string,std::string> H;
//     size_t pos = eol + 2;
//     while (pos < head.size()) {
//         size_t next = head.find("\r\n", pos); if (next==std::string::npos) break;
//         std::string line = head.substr(pos, next-pos); pos = next + 2;
//         if (line.empty()) break;
//         auto c = line.find(':'); if (c==std::string::npos) return false;
//         std::string name = lcase(trim(line.substr(0,c)));
//         std::string val  = trim(line.substr(c+1));
//         H[name] = val;
//     }
//     // keep-alive
//     if (out.version == "HTTP/1.1") {
//         out.keep_alive = !(H.count("connection") && lcase(H["connection"])=="close");
//     } else if (out.version == "HTTP/1.0") {
//         out.keep_alive = (H.count("connection") && lcase(H["connection"])=="keep-alive");
//     } else {
//         out.keep_alive = false; // später ggf. 505
//     }
//     // body semantics
//     out.is_chunked = (H.count("transfer-encoding") && lcase(H["transfer-encoding"]).find("chunked")!=std::string::npos);
//     if (H.count("content-length")) out.content_length = std::strtoull(H["content-length"].c_str(), nullptr, 10);
//     return true;
// }

static void send_error_and_close(size_t i, int code, const std::string& text,
                                 std::vector<pollfd>& fds, std::vector<Client>& clients) {
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

// verarbeitet so viel wie möglich in-place: aus c.rx konsumieren, de-chunked nach 'out' schieben.
// gibt true zurück, wenn Fortschritt; false, wenn mehr Daten benötigt werden.
// static bool dechunk_step(Client& c, std::string& out) {
//     using CS = Client::ChunkState;
//     for(;;){
//         if (c.ch_state == CS::SIZE) {
//             auto p = c.rx.find("\r\n");
//             if (p == std::string::npos) return false;
//             std::string line = c.rx.substr(0, p);
//             c.rx.erase(0, p+2);
//             // hex size; optional chunk extensions ignorieren
//             auto semi = line.find(';');
//             std::string hex = (semi==std::string::npos)? line : line.substr(0,semi);
//             c.ch_need = std::strtoull(hex.c_str(), nullptr, 16);
//             c.ch_state = (c.ch_need==0) ? CS::CRLF_AFTER_DATA : CS::DATA;
//         }
//         if (c.ch_state == CS::DATA) {
//             if (c.rx.size() < c.ch_need) return false;
//             out.append(c.rx.data(), c.ch_need);
//             c.rx.erase(0, c.ch_need);
//             c.ch_state = CS::CRLF_AFTER_DATA;
//         }
//         if (c.ch_state == CS::CRLF_AFTER_DATA) {
//             if (c.rx.size() < 2) return false;
//             if (c.rx.compare(0,2,"\r\n") != 0) return false; // malformed
//             c.rx.erase(0,2);
//             if (c.ch_need == 0) { c.ch_state = CS::DONE; return true; }
//             c.ch_state = CS::SIZE;
//         }
//     }
// }

static void reset_for_next_request(Client& c){
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

static int add_listener(uint16_t port) {
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
    fds.push_back(p);
    clients.push_back(Client{}); // Dummy, hält Index-Sync
    listener_fds.insert(s);

    std::cout << "Listening on 0.0.0.0:" << port << "\n";
    return s;
}












int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);

    const char* cfg_path = (argc > 1) ? argv[1] : "./config/webserv.conf";
    g_cfg.parse_c(cfg_path);

    if (g_cfg.servers.empty()) {
        std::cerr << "Failed to parse config or no servers defined in " << cfg_path << "\n";
        return 1;
    }
    
    
// pro Port nur EINEN Socket binden
std::unordered_map<int /*port*/, int /*lfd*/> lfd_by_port;

    for (size_t s = 0; s < g_cfg.servers.size(); ++s) {
        const ServerConfig& sc = g_cfg.servers[s];
        int port = sc.listen_port;

        // ggf. neuen Listener für diesen Port anlegen
        if (lfd_by_port.find(port) == lfd_by_port.end()) {
            int lfd = add_listener(port);              // deine existierende Funktion (bind 0.0.0.0:port)
            lfd_by_port[port]     = lfd;
            port_by_listener_fd[lfd] = port;
            std::cout << "Listening on *:" << port << " (lfd=" << lfd << ")\n";
        }
        // diesen Server dem Port zuordnen (für vHosts)
        servers_by_port[port].push_back(s);
    }
    

    const long IDLE_MS = 1500000;
    char buf[4096];
    std::cout << "Echo server with write-buffer on port 8080...\n";

    for (;;) {
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

        int ready = poll(&fds[0], fds.size(), 1000);
        if (ready < 0) { if (errno==EINTR) continue; perror("poll"); break; }

        for (size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].revents == 0) continue;

            int fd = fds[i].fd;
            bool is_listener = (listener_fds.find(fd) != listener_fds.end());

            if (is_listener) {
                for (;;) {
                    int cfd = accept(fd, NULL, NULL);
                    if (cfd < 0) {
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

            if (fds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                close(fds[i].fd);
                fds.erase(fds.begin() + i);
                clients.erase(clients.begin() + i);
                --i;
                continue;
            }

            // Lesen
            if (fds[i].revents & POLLIN) {
                for (;;) {
                    ssize_t n = ::read(fds[i].fd, buf, sizeof(buf));
                    if (n > 0) {
                        Client &c = clients[i];
                        c.last_active_ms = now_ms; 
                        c.rx.append(buf, n);

// ------ hier Leo sein Zeug rein
// ------ aus raw string alles rausgeholt und in Request struct

                        // Header komplett?
                        if (c.rx.find("\r\n\r\n") != std::string::npos)
                        {
                            // Header komplett da
                            size_t header_end = c.rx.find("\r\n\r\n") + 4;
                            std::string header_str = c.rx.substr(0, header_end);
                            std::istringstream header_stream(header_str);
                            std::string line;

                            // Erste Zeile: Methode, Pfad, Version
                            if (std::getline(header_stream, line))
                            {
                                if (!line.empty() && line.back() == '\r')
                                    line.pop_back();
                                std::istringstream line_ss(line);
                                line_ss >> c.method >> c.target >> c.version;
                            }

                            // Header-Zeilen
                            while (std::getline(header_stream, line))
                            {
                                if (!line.empty() && line.back() == '\r')
                                    line.pop_back();
                                if (line.empty())
                                    break;
                                size_t pos = line.find(':');
                                if (pos != std::string::npos)
                                {
                                    std::string key = line.substr(0, pos);
                                    std::string value = line.substr(pos + 1);
                                    if (!value.empty() && value[0] == ' ')
                                        value.erase(0, 1);
                                    c.headers[key] = value;
                                }
                            }

                            // Keep-Alive bestimmen
                            if (c.version == "HTTP/1.1")
                            {
                                c.keep_alive = !(c.headers.count("Connection") && c.headers["Connection"] == "close");
                            }
                            else if (c.version == "HTTP/1.0")
                            {
                                c.keep_alive = (c.headers.count("Connection") && c.headers["Connection"] == "keep-alive");
                            }
                            else
                            {
                                // Ungültige Version
                                err505(i, fds, clients);
                                break;
                            }

                            // Body-Handling vorbereiten
                            if (c.headers.count("Transfer-Encoding") && c.headers["Transfer-Encoding"] == "chunked")
                            {
                                c.is_chunked = true;
                            }
                            else if (c.headers.count("Content-Length"))
                            {
                                c.content_len = std::stoul(c.headers["Content-Length"]);
                            }

                            // Body-Teil aus rx entfernen ----- evtl logic fehler weil der im rx bleiben sollte
                            // c.rx.erase(0, header_end);
                            c.state = RxState::READY; // Für dieses Beispiel direkt READY setzen
                        }

                        // Host aus Header sichern
                        if (c.headers.count("Host"))
                            c.host = c.headers["Host"];

                        // Limits an finalen Server anpassen (z. B. 413 später korrekt)
                        const ServerConfig& sc = g_cfg.servers[c.server_idx];
                        c.max_body_bytes = sc.client_max_body_size;

                        // ---- Location bestimmen (Longest Prefix Match) ----
                        auto resolve_location = [](const ServerConfig& sc, const std::string& path)->const LocationConfig&{
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

                        Request req;
                        req = RequestParser().parse(c.rx);
                        c.state = RxState::READY;
                        c.last_active_ms = now_ms;
                
                        if (c.state == RxState::READY && c.tx.empty())
                        {
                            req.conn_fd   = fds[i].fd;
                            req.method    = c.method;
                            req.path      = c.target;
                            req.version   = c.version;
                            req.keep_alive= c.keep_alive;
                            
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
                    } else if (n == 0) {
                        ::close(fds[i].fd);
                        fds.erase(fds.begin()+i);
                        clients.erase(clients.begin()+i);
                        --i; break;
                    } else {
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
            if (fds[i].revents & POLLOUT) {
                Client &c = clients[i];
                while (!c.tx.empty()) {
                    ssize_t m = write(fds[i].fd, c.tx.data(), c.tx.size());
                    if (m > 0) { c.tx.erase(0, m); c.last_active_ms = now_ms; continue; }
                    if (m < 0 && (errno==EAGAIN || errno==EWOULDBLOCK)) break;
                    if (m < 0) { perror("write"); break; }
                }
                if (c.tx.empty()) {
                    if (c.keep_alive) {
                        reset_for_next_request(c);
                        fds[i].events &= ~POLLOUT;          // zurück auf nur lesen
                        // Verbindung offen lassen
                    } else {
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
