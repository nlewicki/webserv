/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Response.cpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: nlewicki <nlewicki@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:31 by mhummel           #+#    #+#             */
/*   Updated: 2025/11/06 10:32:09 by nlewicki         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Response.hpp"
#include "CGIHandler.hpp"
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
    for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
        ss << it->first << ": " << it->second << "\r\n";
    ss << "\r\n";
    ss << body;
    return ss.str();
}

static bool isCGIRequest(const std::string& path)
{
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
        return false;

    std::string ext = path.substr(dot + 1);
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

static bool containsPathTraversal(const std::string& s) {
    if (s.find("..") != std::string::npos) return true;
    return false;
}

// Einfaches join (achtet auf Slashes)
static std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::string out = a;
    if (out.back() != '/') out += '/';
    if (b.front() == '/') out += b.substr(1); else out += b;
    return out;
}

// MIME-Mapping (erweiterbar)
static std::string getMimeType(const std::string& path) {
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
static std::string generateDirectoryListing(const std::string& dirPath, const std::string& urlPrefix) {
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

Response ResponseHandler::handleRequest(const Request& req, const LocationConfig& config)
{
	Response res;
	printf("config_path: %s\n", config.path.c_str());

	res.keep_alive = req.keep_alive;
	
	// default headers
	res.headers["Server"] = "webserv/1.0";
    // res.headers["Connection"] = "close";
	res.headers["Keep-Alive"] = req.keep_alive ? "timeout=5, max=100" : "timeout=0, max=0";
    res.headers["Content-Type"] = "text/html";

	std::string path = config.root + "/" + config.index; // default path 
		
	printf("path: %s\n", path.c_str());
	if (isCGIRequest(req.path))
	{
		CGIHandler cgi;
		return cgi.execute(req);
	}
	if (req.method == "GET")
	{
		// 1) URL-decode und normalize
		std::string url = urlDecode(req.path);
		if (url.empty()) url = "/";
		url = normalizePath(url);

		if (containsPathTraversal(url)) {
			res.statusCode = 403;
			res.reasonPhrase = "Forbidden";
			res.body = "<h1>403 Forbidden</h1>";
			res.headers["Content-Type"] = "text/html";
			res.headers["Content-Length"] = std::to_string(res.body.size());
			return res;
		}

		// 2) Find location root
		//    Build filesystem path relative to location.root
		std::string fsPath = config.root;
		if (fsPath.empty()) fsPath = "."; // fallback
		// strip location path prefix if present: assume req.path is the full URL path; 
		// if location.path is not "/", remove prefix
		std::string trimmedUrl = url;
		if (!config.path.empty() && config.path != "/" && trimmedUrl.find(config.path) == 0) {
			trimmedUrl = trimmedUrl.substr(config.path.length());
			if (trimmedUrl.empty()) trimmedUrl = "/";
		}
		fsPath = joinPath(fsPath, trimmedUrl);

		// 3) If path is directory -> serve index or autoindex
		if (isDirectory(fsPath)) {
			// ensure trailing slash in URL behavior handled elsewhere; here we just check
			std::string indexFile = joinPath(fsPath, config.index.empty() ? "index.html" : config.index);
			if (fileExists(indexFile))
			{
				// serve index file
				res.statusCode = 200;
				res.reasonPhrase = getStatusMessage(200);
				res.body = readFile(indexFile);
				res.headers["Content-Type"] = getMimeType(indexFile);
				res.headers["Content-Length"] = std::to_string(res.body.size());
				return res;
			}
			else if (config.autoindex)
			{
				// generate listing
				std::string urlPrefix = url; // used for links
				std::string listing = generateDirectoryListing(fsPath, urlPrefix);
				res.statusCode = 200;
				res.reasonPhrase = getStatusMessage(200);
				res.body = listing;
				res.headers["Content-Type"] = "text/html";
				res.headers["Content-Length"] = std::to_string(res.body.size());
				return res;
			}
			else
			{
				res.statusCode = 403;
				res.reasonPhrase = "Forbidden";
				res.body = "<h1>403 Forbidden</h1><p>Index disabled.</p>";
				res.headers["Content-Type"] = "text/html";
				res.headers["Content-Length"] = std::to_string(res.body.size());
				return res;
			}
		}

		// 4) If path is file -> CGI? or static
		if (fileExists(fsPath)) {
			// If CGI extension detected, forward to CGI handler (you may need to pass filesystem path in req)
			if (isCGIRequest(fsPath))
			{
				CGIHandler cgi;
				return cgi.execute(req); // consider setting env/path in req for CGI
			}

			// Serve file
			res.statusCode = 200;
			res.reasonPhrase = getStatusMessage(200);
			res.body = readFile(fsPath);
			res.headers["Content-Type"] = getMimeType(fsPath);
			res.headers["Content-Length"] = std::to_string(res.body.size());
			return res;
		}
		else
		{
			// Not found
			res.statusCode = 404;
			res.reasonPhrase = getStatusMessage(404);
			res.body = "<h1>404 Not Found</h1>";
			res.headers["Content-Type"] = "text/html";
			res.headers["Content-Length"] = std::to_string(res.body.size());
			return res;
		}
	}

	else if (req.method == "POST")
	{
		std::string dir = config.data_dir.empty() ? "./data" : config.data_dir;
		std::string filename = dir + "/post_" + std::to_string(time(NULL)) + ".txt";
		std::ofstream out(filename.c_str());
		if (!out.is_open())
		{
			res.statusCode = 500;
			res.reasonPhrase = "Internal Server Error";
			res.body = "<h1>500 Internal Server Error</h1><p>Could not write to data folder.</p>";
		}
		else
		{
			out << req.body;
			out.close();

			res.statusCode = 200;
			res.reasonPhrase = getStatusMessage(200);
			res.body = "<h1>POST stored successfully!</h1><p>Saved as " + filename + "</p>";
		}
	}
	else if (req.method == "DELETE")
	{
		std::string dir = config.data_dir.empty() ? "./data" : config.data_dir;
		std::string filepath = dir;
		filepath += "/" + req.body; // assuming the filename to delete is in the body
		std::cout << "DELETE path: " << filepath << std::endl;

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
	}
	else
	{
		res.statusCode = 405;
		res.reasonPhrase = getStatusMessage(405);
        res.body = "<h1>405 Method Not Allowed</h1>";
	}
	res.headers ["Content-Length"] = std::to_string(res.body.size());
	std::cout << res.toString() << std::endl;
	return res;
}
