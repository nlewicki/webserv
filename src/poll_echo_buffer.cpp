#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>


int make_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

struct Client {
    std::string rx;
    std::string tx;
    long last_active_ms;
    bool        header_done = false;
};

static std::vector<pollfd>     fds;
static std::unordered_set<int> listener_fds;
static std::vector<Client>     clients;

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

int main() {
    signal(SIGPIPE, SIG_IGN);
    
    add_listener(8080);
    add_listener(8081);
 
    const long IDLE_MS = 15000;
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
                    clients.push_back(c);

                    std::cout << "New client " << cfd << " (from listener " << fd << ")\n";
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
                    ssize_t n = read(fds[i].fd, buf, sizeof(buf));
                    if (n > 0) {
                        Client &c = clients[i];
                        c.rx.append(buf, n);
                        c.last_active_ms = now_ms;

                        if (!c.header_done) {
                            size_t pos = c.rx.find("\r\n\r\n");
                            if (pos != std::string::npos) {
                                c.header_done = true;

                                static const std::string body = "Hello world!";
                                c.tx  = "HTTP/1.1 200 OK\r\n"
                                        "Content-Type: text/plain\r\n"
                                        "Content-Length: " + std::to_string(body.size()) + "\r\n"
                                        "Connection: close\r\n"
                                        "\r\n" + body;

                                fds[i].events |= POLLOUT;   // wir wollen senden
                            }
                        }
                        continue; // evtl. weitere Bytes lesen
                    } else if (n == 0) {
                        close(fds[i].fd);
                        fds.erase(fds.begin() + i);
                        clients.erase(clients.begin() + i);
                        --i; break;
                    } else if (errno==EAGAIN || errno==EWOULDBLOCK) {
                        break; // alles gelesen
                    } else {
                        perror("read");
                        close(fds[i].fd);
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
                    // Für den Anfang nach Antwort schließen (Connection: close)
                    close(fds[i].fd);
                    fds.erase(fds.begin()+i);
                    clients.erase(clients.begin()+i);
                    --i;
                }
            }
        }
    }

    for (auto &p : fds) ::close(p.fd);
    return 0;
}
