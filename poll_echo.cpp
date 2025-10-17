#include <iostream>
#include <vector>
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

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen"); return 1;
    }

    make_nonblocking(server_fd);

    std::vector<pollfd> fds;
    fds.push_back({server_fd, POLLIN, 0});

    std::cout << "Echo server on port 8080...\n";

    char buf[4096];
    while (true) {
        int ready = poll(&fds[0], fds.size(), -1);
        if (ready < 0) { perror("poll"); break; }

        for (size_t i = 0; i < fds.size(); ++i) {
            if (fds[i].revents == 0) continue;

            if (fds[i].fd == server_fd) {
                int client_fd = accept(server_fd, NULL, NULL);
                make_nonblocking(client_fd);
                fds.push_back({client_fd, POLLIN, 0});
                std::cout << "New client: " << client_fd << "\n";
            } else {
                ssize_t n = read(fds[i].fd, buf, sizeof(buf));
                if (n > 0) {
                    write(fds[i].fd, buf, n);
                } else if (n == 0) {
                    close(fds[i].fd);
                    fds.erase(fds.begin() + i);
                    --i;
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("read");
                    close(fds[i].fd);
                    fds.erase(fds.begin() + i);
                    --i;
                }
            }
        }
    }

    close(server_fd);
    return 0;
}




std::vector<pollfd> fds;
std::unordered_set<int> listener_fds;

int add_listener(uint16_t port) {
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return -1; }
    int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in a{}; 
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);    // 0.0.0.0
    a.sin_port = htons(port);
    if (::bind(s, (sockaddr*)&a, sizeof(a)) < 0) { perror("bind"); ::close(s); return -1; }
    if (::listen(s, 128) < 0) { perror("listen"); ::close(s); return -1; }
    make_nonblocking(s);

    pollfd p{}; p.fd = s; p.events = POLLIN; p.revents = 0;
    fds.push_back(p);
    listener_fds.insert(s);
    std::cout << "Listening on 0.0.0.0:" << port << "\n";
    return s;
}