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

int make_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

struct Client {
    std::string buffer;   // Daten, die noch gesendet werden müssen
};

int main() {
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
        int ready = poll(&fds[0], fds.size(), -1);
        if (ready < 0) { perror("poll"); break; }

        for (size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].revents == 0) continue;

            if (fds[i].fd == server_fd) {
                int cfd = accept(server_fd, NULL, NULL);
                if (cfd < 0) continue;
                make_nonblocking(cfd);
                fds.push_back({cfd, POLLIN, 0});
                clients.push_back(Client());
                std::cout << "New client " << cfd << "\n";
            } 
            else {
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
                    ssize_t n = read(fds[i].fd, buf, sizeof(buf));
                    if (n > 0) {
                        clients[i].buffer.append(buf, n); // Echo speichern
                        // Jetzt wollen wir schreiben, sobald möglich
                        fds[i].events |= POLLOUT;
                    } else if (n == 0) {
                        close(fds[i].fd);
                        fds.erase(fds.begin() + i);
                        clients.erase(clients.begin() + i);
                        --i;
                    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("read");
                    }
                }

                // Schreiben
                if (fds[i].revents & POLLOUT) {
                    std::string &out = clients[i].buffer;
                    if (!out.empty()) {
                        ssize_t m = write(fds[i].fd, out.data(), out.size());
                        if (m > 0) {
                            out.erase(0, m); // entferne gesendeten Teil
                        }
                    }
                    // Wenn alles raus ist → POLLOUT wieder deaktivieren
                    if (clients[i].buffer.empty())
                        fds[i].events = POLLIN;
                }
            }
        }
    }

    close(server_fd);
    return 0;
}
