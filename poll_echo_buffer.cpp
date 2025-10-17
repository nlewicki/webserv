#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <csignal>
#include <ctime>
#include <chrono>

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

int main() {
    signal(SIGPIPE, SIG_IGN);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(8080);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);
    make_nonblocking(server_fd);

    std::vector<pollfd> fds;
    fds.push_back({server_fd, POLLIN, 0});
    std::vector<Client> clients(1); // Index 0 = Server

    char buf[4096];
    std::cout << "Echo server with write-buffer on port 8080...\n";

    while (true) {
        const long IDLE_MS = 3000; // 3s
        using clock_t = std::chrono::steady_clock;
        using ms      = std::chrono::milliseconds;

        auto now_ms = std::chrono::duration_cast<ms>(clock_t::now().time_since_epoch()).count();
        for (size_t i = 1; i < fds.size(); ++i) {
            if (now_ms - clients[i].last_active_ms > IDLE_MS) {
                std::cerr << "[TIMEOUT] fd=" << fds[i].fd
                        << " idle=" << (now_ms - clients[i].last_active_ms) << "ms\n";
                close(fds[i].fd);
                fds.erase(fds.begin()+i);
                clients.erase(clients.begin()+i);
                --i;
            }
        }

        int ready = poll(&fds[0], fds.size(), 1000);
        if (ready < 0) { if (errno==EINTR) continue; perror("poll"); break; }

        for (size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].revents == 0) continue;

            if (fds[i].fd == server_fd) {
                for (;;) {
                int cfd = accept(server_fd, NULL, NULL);
                if (cfd < 0) {
                    if (errno==EAGAIN || errno==EWOULDBLOCK) break;
                    perror("accept"); break;
                }
                make_nonblocking(cfd);
                fds.push_back({cfd, POLLIN, 0});
                clients.push_back(Client());
                clients.back().last_active_ms = now_ms;
                std::cout << "New client " << cfd << "\n";
                } 
            }  else {
                // Fehler / Disconnect
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
    }

    close(server_fd);
    return 0;
}
