#pragma once
#include <string>
#include <unordered_map>

struct CoreRequest {
    int         conn_fd = -1;
    std::string method;     // z. B. "GET"
    std::string target;     // z. B. "/"
    std::string version;    // "HTTP/1.1"
    std::unordered_map<std::string,std::string> headers; // optional: füllst du später
    std::string body;       // de-chunkt / Content-Length vollständig
    bool        keep_alive = false; // aus Version+Header abgeleitet
};

struct CoreResponse {
    std::string raw;        // kompletter HTTP-Response (Header+CRLFCRLF+Body)
    bool        keep_alive = false; // ob Verbindung offen bleiben soll
};

// Dein Kumpel ersetzt diese Funktion später durch sein echtes HTTP-Modul.
CoreResponse http_handle(const CoreRequest& req);
