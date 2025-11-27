/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Response.cpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: nlewicki <nlewicki@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:24 by mhummel          #+#    #+#             */
/*   Updated: 2025/11/18 12:36:58 by nlewicki         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */


#include "../include/Response.hpp"
#include "../include/CGIHandler.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <iostream>
#include <iomanip>
#include <dirent.h>
#include <algorithm>
#include <cctype>

ResponseHandler::ResponseHandler() {}
ResponseHandler::~ResponseHandler() {}

// Response-Object to HTTP-string
std::string Response::toString() const
{
    std::ostringstream ss;
    ss << "HTTP/1.1 " << statusCode << " " << reasonPhrase << "\r\n";
	for (size_t i = 0; i < set_cookies.size(); ++i)																// set cookies
        ss << "Set-Cookie: " << set_cookies[i] << "\r\n";
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)	// headers
        ss << it->first << ": " << it->second << "\r\n";
    ss << "\r\n";
    ss << body;
    return ss.str();
}

// Setzt ein Cookie im Response
void Response::setCookie(const std::string& name, const std::string& value, const std::string& path, int maxAge, bool httpOnly,
						 const std::string& sameSite)
{
	std::ostringstream sc;
	sc << name << "=" << value;
	if (maxAge >= 0) sc << "; Max-Age=" << maxAge;
	if (!path.empty()) sc << "; Path=" << path;
	if (httpOnly) sc << "; HttpOnly";
	if (!sameSite.empty()) sc << "; SameSite=" << sameSite; // "Lax"|"Strict"|"None"
	set_cookies.push_back(sc.str());
}

static bool isCGIRequest(const std::string& path)
{
    // Arbeitskopie
    std::string p = path;

    // Entferne CR/LF und führende/trailing whitespace
    while (!p.empty() && (p.back() == '\r' || p.back() == '\n' || isspace((unsigned char)p.back())))
        p.pop_back();
    size_t start = 0;
    while (start < p.size() && isspace((unsigned char)p[start])) ++start;
    if (start) p = p.substr(start);

    // Entferne Query-String / Fragment (teile nach ? oder #)
    size_t q = p.find_first_of("?#");
    if (q != std::string::npos) p.resize(q);

    // Nimm nur letzten Pfad-Element (falls ein voller Pfad übergeben wurde)
    size_t lastSlash = p.find_last_of('/');
    std::string last = (lastSlash == std::string::npos) ? p : p.substr(lastSlash + 1);

    // Finde Extension
    size_t dot = last.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = last.substr(dot + 1);

    // lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    return (ext == "py" || ext == "php" || ext == "cgi");
}

// URL-decode (simple)
static std::string urlDecode(const std::string& s) {
    std::string ret;
    ret.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = { s[i+1], s[i+2], 0 };
            ret += static_cast<char>(std::strtol(hex, nullptr, 16));
            i += 2;
        } else if (s[i] == '+') {
            ret += ' ';
        } else {
            ret += s[i];
        }
    }
    return ret;
}


// Normiert Pfad: entfernt doppelte Slashes, einfache Normalisierung
static std::string normalizePath(const std::string& path) {
    std::string out;
    out.reserve(path.size());
    bool lastSlash = false;
    for (char c : path) {
        if (c == '/') {
            if (!lastSlash) { out += '/'; lastSlash = true; }
        } else {
            out += c; lastSlash = false;
        }
    }
    if (out.size() > 1 && out.back() == '/') out.pop_back(); // entferne letzten Slash (außer "/" selbst)
    if (out.empty()) out = "/";
    return out;
}

static bool containsPathTraversal(const std::string& s)
{
    if (s.find("..") != std::string::npos) return true;
    return false;
}

// Einfaches join (achtet auf Slashes)
static std::string joinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::string out = a;
    if (out.back() != '/') out += '/';
    if (b.front() == '/') out += b.substr(1); else out += b;
    return out;
}

// MIME-Mapping (erweiterbar)
static std::string getMimeType(const std::string& path)
{
    static const std::map<std::string, std::string> m = {
        { "html", "text/html" }, { "htm", "text/html" }, { "css", "text/css" },
        { "js", "application/javascript" }, { "json", "application/json" },
        { "png", "image/png" }, { "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" },
        { "gif", "image/gif" }, { "svg", "image/svg+xml" }, { "txt", "text/plain" },
        { "pdf", "application/pdf" }, { "ico", "image/x-icon" }
    };
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot + 1);
    // lower
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    auto it = m.find(ext);
    if (it != m.end()) return it->second;
    return "application/octet-stream";
}

// Generiere einfaches Verzeichnis-Listing (HTML)
static std::string generateDirectoryListing(const std::string& dirPath, const std::string& urlPrefix)
{
    DIR* dp = opendir(dirPath.c_str());
    if (!dp) return "<h1>500 Cannot open directory</h1>";
    std::ostringstream out;
    out << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of "
        << urlPrefix << "</title></head><body>";
    out << "<h1>Index of " << urlPrefix << "</h1><ul>";
    struct dirent* e;
    while ((e = readdir(dp)) != NULL) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        // Escape name? minimal:
        out << "<li><a href=\"" << (urlPrefix.back()=='/'? urlPrefix : urlPrefix + "/") << name << "\">"
            << name << "</a></li>";
    }
    out << "</ul></body></html>";
    closedir(dp);
    return out.str();
}

// check file or dir via stat
static bool isDirectory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

std::string ResponseHandler::getStatusMessage(int code)
{
	switch (code)
	{
		case 200: return "OK";
		case 404: return "Not Found";
		case 405: return "Method not Allowed";
		default : return "Unkown";
	}
}

std::string ResponseHandler::readFile(const std::string& path)
{
	std::ifstream file(path.c_str());
	if (!file.is_open())
		return "<h1>Error opening file</h1>";;

	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

bool ResponseHandler::fileExists(const std::string& path)
{
	struct stat buf;
	return (stat(path.c_str(), &buf) == 0);
}


void setHeaders(Response& res, const Request& req)
{
	res.headers["Server"] = "webserv/1.0";
	res.headers["Connection"] = req.keep_alive ? "keep-alive" : "close";
	res.headers["Keep-Alive"] = req.keep_alive ? "timeout=5, max=100" : "timeout=0, max=0";
    res.headers["Content-Type"] = "text/html";

}

// sanitize/normalize color cookie value, returns empty string on invalid input
static std::string sanitizeColor(const std::string& raw)
{
    if (raw.empty()) return "";
    std::string s = raw;
    // if percent-encoded, decode first
    s = urlDecode(s);
    // allow "#rrggbb" or "rrggbb"
    if (s.size() == 6 && s[0] != '#') s = "#" + s;
    if (s.size() != 7) return "";
    for (size_t i = 1; i < s.size(); ++i) {
        char c = s[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!ok) return "";
    }
    return s;
}

// helper: return color cookie value
static std::string cookieColor(const Request& req)
{
    if (req.cookies.count("color")) return req.cookies.at("color");
    if (req.cookies.count("bg"))    return req.cookies.at("bg");
    return "";
}

// response with given status and body (keeps Content-Type html by default)
Response ResponseHandler::makeHtmlResponse(int status, const std::string& body)
{
    Response r;
    r.statusCode = status;
    r.reasonPhrase = ResponseHandler().getStatusMessage(status);
    r.body = body;
    r.headers["Content-Type"] = "text/html";
    r.headers["Content-Length"] = std::to_string(r.body.size());
    return r;
}

// handle directory: index file, autoindex, or forbidden
bool ResponseHandler::handleDirectoryRequest(const std::string& url, const std::string& fsPath,
                                   const LocationConfig& config, Response& res)
{
    std::string indexFile = joinPath(fsPath, config.index.empty() ? "index.html" : config.index);
    if (fileExists(indexFile)) {
        res.statusCode = 200;
        res.reasonPhrase = ResponseHandler().getStatusMessage(200);
        res.body = readFile(indexFile);
        res.headers["Content-Type"] = getMimeType(indexFile);
        res.headers["Content-Length"] = std::to_string(res.body.size());
        return true;
    }
    if (config.autoindex) {
        res.statusCode = 200;
        res.reasonPhrase = ResponseHandler().getStatusMessage(200);
        res.body = generateDirectoryListing(fsPath, url);
        res.headers["Content-Type"] = "text/html";
        res.headers["Content-Length"] = std::to_string(res.body.size());
        return true;
    }
    // index disabled
    res = makeHtmlResponse(404, "<h1>404 Not Found</h1>");
    return true;
}

// handle static file or CGI. returns true if handled (res filled), false if not found.
bool ResponseHandler::handleFileOrCgi(const Request& req, const std::string& fsPath,
                            Response& res)
{
    if (!fileExists(fsPath)) return false;

    if (isCGIRequest(fsPath)) {
        CGIHandler cgi;
        Request req_cgi = req;
        req_cgi.path = fsPath; // pass filesystem path to CGI
        res = cgi.execute(req_cgi);
        res.keep_alive = req.keep_alive;
        // ensure Content-Length set by CGI or default
        if (!res.headers.count("Content-Length")) res.headers["Content-Length"] = std::to_string(res.body.size());
        return true;
    }

    // static file
    res.statusCode = 200;
    res.reasonPhrase = ResponseHandler().getStatusMessage(200);
    res.body = readFile(fsPath);
    res.headers["Content-Type"] = getMimeType(fsPath);
    res.headers["Content-Length"] = std::to_string(res.body.size());
    return true;
}

// extract validated color from req cookies (wrapper of existing helpers)
static std::string extractValidatedColor(const Request& req)
{
    return sanitizeColor(cookieColor(req));
}

Response& ResponseHandler::methodGET(const Request& req, Response& res, const LocationConfig& config)
{
    std::string url = urlDecode(req.path);
    if (url.empty()) url = "/";
    url = normalizePath(url);

    // security check
    if (containsPathTraversal(url)) {
        res = makeHtmlResponse(403, "<h1>403 Forbidden</h1>");
        return res;
    }

    // prepare filesystem path relative to location root
    std::string fsPath = config.root.empty() ? std::string(".") : config.root;
    std::string trimmedUrl = url;
    if (!config.path.empty() && config.path != "/" && trimmedUrl.find(config.path) == 0) {
        trimmedUrl = trimmedUrl.substr(config.path.length());
        if (trimmedUrl.empty()) trimmedUrl = "/";
    }
    fsPath = joinPath(fsPath, trimmedUrl);

    // attach user color early so static html can be patched later
    std::string color = extractValidatedColor(req);

    // directory handling
    if (isDirectory(fsPath)) {
        handleDirectoryRequest(url, fsPath, config, res);
        return res;
    }

    // file handling (CGI or static)
    if (handleFileOrCgi(req, fsPath, res)) {
        // inject color only for text/html static responses (leave CGI response intact)
        if (res.headers["Content-Type"] == "text/html") {
            size_t pos = res.body.find("<body");
            if (pos != std::string::npos) {
                size_t end = res.body.find(">", pos);
                if (end != std::string::npos) {
                    std::string insert = " style=\"--user-color: " + (color.empty() ? std::string("#ffffff") : color) + ";\"";
                    res.body.insert(end, insert);
                    res.headers["Content-Length"] = std::to_string(res.body.size());
                }
            }
        }
        return res;
    }

    // not found
    res = makeHtmlResponse(404, "<h1>404 Not Found</h1>");
    return res;
}

Response& ResponseHandler::methodPOST(const Request& req, Response& res, const LocationConfig& config)
{
	std::string dir = config.root;
	if (dir.empty())
		dir = "./root/data/"; // Fallback, sollte eigentlich nicht nötig sein

#ifdef DEBUG
	std::cout << "POST data dir: " << dir << std::endl;
#endif
	std::string contentType;
	if (req.headers.count("Content-Type"))
		contentType = req.headers.find("Content-Type")->second;

	// --- Multipart upload ---
	if (contentType.find("multipart/form-data") != std::string::npos)
	{
		// 1. Boundary extrahieren
		size_t pos = contentType.find("boundary=");
		if (pos == std::string::npos)
		{
			res.statusCode = 400;
			res.reasonPhrase = "Bad Request";
			res.body = "<h1>400 Bad Request</h1><p>Missing multipart boundary.</p>";
		}
		else
		{
			std::string boundary = "--" + contentType.substr(pos + 9);
			std::string body = req.body;

			// 2. Datei extrahieren (vereinfachte Variante)
			size_t fileStart = body.find("filename=\"");
			if (fileStart == std::string::npos)
			{
				res.statusCode = 400;
				res.reasonPhrase = "Bad Request";
				res.body = "<h1>400 Bad Request</h1><p>No file field found.</p>";
			}
			else
			{
				fileStart += 10;
				size_t fileEnd = body.find("\"", fileStart);
				std::string originalName = body.substr(fileStart, fileEnd - fileStart);

				// 3. Dateidatenbereich suchen
				size_t dataStart = body.find("\r\n\r\n", fileEnd);
				if (dataStart == std::string::npos)
				{
					res.statusCode = 400;
					res.reasonPhrase = "Bad Request";
					res.body = "<h1>400 Bad Request</h1><p>Malformed multipart data.</p>";
				}
				else
				{
					dataStart += 4;
					size_t dataEnd = body.find(boundary, dataStart);
					std::string fileContent = body.substr(dataStart, dataEnd - dataStart);
					// Strip trailing \r\n if present
					if (fileContent.size() >= 2 && fileContent[fileContent.size() - 2] == '\r')
						fileContent.erase(fileContent.size() - 2);

					// 4. Sicherer Dateiname (keine Pfad-Traversal)
					for (size_t i = 0; i < originalName.size(); ++i)
						if (originalName[i] == '/' || originalName[i] == '\\')
							originalName[i] = '_';

					std::string filePath = dir + "/" + originalName;

					// 5. Datei speichern
					std::ofstream out(filePath.c_str(), std::ios::binary);
					if (!out.is_open())
					{
						res.statusCode = 500;
						res.reasonPhrase = "Internal Server Error";
						res.body = "<h1>500 Internal Server Error</h1><p>Could not write to data folder.</p>";
					}
					else
					{
						out.write(fileContent.data(), fileContent.size());
						out.close();

						res.statusCode = 200;
						res.reasonPhrase = getStatusMessage(200);
						res.body = "<h1>Upload successful!</h1><p>Saved as " + filePath + "</p>";
					}
				}
			}
		}
	}

	// --- Fallback: raw upload ---
	else
	{
		std::string filename = dir + "/upload_" + std::to_string(time(NULL));
		std::ofstream out(filename.c_str(), std::ios::binary);
		if (!out.is_open())
		{
			res.statusCode = 500;
			res.reasonPhrase = "Internal Server Error";
			res.body = "<h1>500 Internal Server Error</h1><p>Could not write to data folder.</p>";
		}
		else
		{
			out.write(req.body.data(), req.body.size());
			out.close();

			res.statusCode = 200;
			res.reasonPhrase = getStatusMessage(200);
			res.body = "<h1>POST stored successfully!</h1><p>Saved as " + filename + "</p>";
		}
	}
	return res;
}

Response& ResponseHandler::methodDELETE(const Request& req, Response& res, const LocationConfig& config)
{
	std::string dir = config.data_dir.empty() ? "./data" : config.data_dir;
	std::string filepath = "root/" + dir;
	filepath += "/" + req.body; // assuming the filename to delete is in the body

	#ifdef DEBUG
	std::cout << "DELETE path: " << filepath << std::endl;
	#endif

	if (fileExists(filepath) && std::remove(filepath.c_str()) == 0)
	{
		res.statusCode = 200;
		res.reasonPhrase = getStatusMessage(200);
		res.body = "<h1>File deleted successfully.</h1>";
	}
	else
	{
		res.statusCode = 404;
		res.reasonPhrase = getStatusMessage(404);
		res.body = "<h1>404 File not found.</h1>";
	}
	return res;
}

Response ResponseHandler::handleRequest(const Request& req, const LocationConfig& config)
{
    Response res;
    res.keep_alive = req.keep_alive;

    // default headers & cookies
    setHeaders(res, req);

    // ────────────────────── 413 PAYLOAD TOO LARGE CHECK ──────────────────────
   // → Ersetze durch:
	size_t max_body_size = 10 * 1024 * 1024;  // 10MB fallback (wie nginx default)

	if (config.has_client_max_body_size && config.client_max_body_size > 0) {
		max_body_size = config.client_max_body_size;
	}

	if ((req.method == "POST" || req.method == "PUT") && req.body.size() > max_body_size) {
		res.statusCode = 413;
		res.reasonPhrase = "Payload Too Large";
		res.body = "<h1>413 Payload Too Large</h1>";
		res.headers["Content-Type"] = "text/html";
		res.headers["Content-Length"] = std::to_string(res.body.size());
		return res;
	}
    // ────────────────────────────────────────────────────────────────────────

    std::string path = config.root + "/" + config.index;

#ifdef DEBUG
    printf("path: %s\n", path.c_str());
#endif

    // debug check allowed mehtods
    std::cout << "Allowed methods: ";
    for (size_t i = 0; i < config.methods.size(); ++i)
        std::cout << config.methods[i] << " ";
    std::cout << std::endl;

	if (req.method == "GET" && config.methods.end() !=
        std::find(config.methods.begin(), config.methods.end(), "GET"))
	{
		return methodGET(req, res, config);
	}
	else if (req.method == "POST" && config.methods.end() !=
        std::find(config.methods.begin(), config.methods.end(), "POST"))
	{
		return methodPOST(req, res, config);
	}
	else if (req.method == "DELETE" && config.methods.end() !=
        std::find(config.methods.begin(), config.methods.end(), "DELETE"))
	{
		return methodDELETE(req, res, config);
	}
	else
	{
		res.statusCode = 405;
		res.reasonPhrase = getStatusMessage(405);
        res.body = "<h1>405 Method Not Allowed</h1>";
    }

    res.headers["Content-Length"] = std::to_string(res.body.size());

#ifdef DEBUG
    std::cout << "method : " << req.method << std::endl;
    std::cout << "path : " << path << std::endl;
    std::cout << "body size: " << req.body.size() << " / max: " << max_body_size << std::endl;
    std::cout << res.toString() << std::endl;
#endif

    return res;
}

