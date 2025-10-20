// src/http_bridge_static.cpp
#include "http_bridge.hpp"
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <vector> 
#include <string>

static bool file_exists(const std::string& p) {
    struct stat st; return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
static bool dir_exists(const std::string& p) {
    struct stat st; return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
static std::string read_all(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
static std::string guess_mime(const std::string& path) {
    static const std::unordered_map<std::string,std::string> M = {
        {".html","text/html"}, {".htm","text/html"}, {".css","text/css"},
        {".js","application/javascript"}, {".json","application/json"},
        {".png","image/png"}, {".jpg","image/jpeg"}, {".jpeg","image/jpeg"},
        {".gif","image/gif"}, {".svg","image/svg+xml"}, {".txt","text/plain"}
    };
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot);
    auto it = M.find(ext); return it==M.end() ? "application/octet-stream" : it->second;
}
static std::string status_line(int code, const char* text) {
    std::ostringstream oss; oss << "HTTP/1.1 " << code << " " << text << "\r\n"; return oss.str();
}
// einfache, sichere Pfad-Normalisierung (kein %xx-Decoding hier – reicht für Start)
static std::string sanitize_target(std::string t) {
    if (t.empty() || t[0] != '/') return std::string(); // invalid
    // remove query part
    if (auto q = t.find('?'); q != std::string::npos) t.erase(q);
    // collapse .. and .
    std::vector<std::string> parts; std::string cur;
    std::istringstream iss(t);
    while (std::getline(iss, cur, '/')) {
        if (cur.empty() || cur == ".") continue;
        if (cur == "..") { if (!parts.empty()) parts.pop_back(); continue; }
        parts.push_back(cur);
    }
    std::string out = "/";
    for (size_t i=0;i<parts.size();++i) {
        out += parts[i];
        if (i+1<parts.size()) out += "/";
    }
    return out;
}

static CoreResponse make_error(int code, const char* text, bool keep_alive) {
    CoreResponse r; r.keep_alive = keep_alive;
    std::string b = std::to_string(code) + " " + text + "\n";
    std::ostringstream resp;
    resp << status_line(code, text)
         << "Content-Type: text/plain\r\n"
         << "Content-Length: " << b.size() << "\r\n"
         << "Connection: " << (keep_alive ? "keep-alive" : "close") << "\r\n\r\n"
         << b;
    r.raw = resp.str(); return r;
}

CoreResponse http_handle(const CoreRequest& req) {
    // Nur GET/HEAD/POST (für jetzt GET/HEAD; POST lässt Body ungenutzt)
    if (req.method != "GET" && req.method != "HEAD") {
        CoreResponse r = make_error(405, "Method Not Allowed", /*keep*/false);
        // RFC verlangt Allow-Header
        r.raw.insert(r.raw.find("\r\n\r\n"), "Allow: GET, HEAD\r\n");
        return r;
    }
    // Ziel normalisieren
    std::string t = sanitize_target(req.target);
    if (t.empty()) return make_error(400, "Bad Request", false);

    // Root einfach hart: ./www (später aus Config)
    std::string root = "www";
    std::string path = (t == "/") ? (root + "/index.html")
                                  : (root + t);

    // Directory → index.html versuchen
    if (dir_exists(path)) {
        if (path.back() != '/') path += "/";
        path += "index.html";
    }

    if (!file_exists(path)) {
        return make_error(404, "Not Found", req.keep_alive);
    }

    std::string body = read_all(path);
    std::string mime = guess_mime(path);

    CoreResponse r;
    r.keep_alive = req.keep_alive; // respektiere Keep-Alive (Core darf später final entscheiden)
    std::ostringstream resp;
    resp << status_line(200, "OK")
         << "Content-Type: " << mime << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: " << (r.keep_alive ? "keep-alive" : "close") << "\r\n\r\n";

    if (req.method == "GET") resp << body; // HEAD sendet nur Header
    r.raw = resp.str();
    return r;
}
