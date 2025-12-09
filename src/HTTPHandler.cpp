/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HTTPHandler.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: nicolewicki <nicolewicki@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:22 by mhummel           #+#    #+#             */
/*   Updated: 2025/12/08 21:19:58 by nicolewicki      ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "../include/HTTPHandler.hpp"
#include <iostream>
#include <sstream>

RequestParser::RequestParser() {};

RequestParser::~RequestParser() {};

static bool decodeChunkedBody(
    std::istream& stream,
    std::string& out,
    std::string& err,
    size_t maxBody,
    bool &tooLarge
)
{
    out.clear();
    err.clear();
    tooLarge = false;

    std::string line;

    while (true) {
        // Lese die Size-Line (hex [;extensions]) terminated by CRLF
        if (!std::getline(stream, line)) {
            err = "unexpected EOF reading chunk size";
            return false;
        }
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        // ignore any empty lines before the size (robustness)
        if (line.empty())
            continue;

        size_t sem = line.find(';');
        std::string sizeStr = (sem == std::string::npos)
                              ? line
                              : line.substr(0, sem);

        size_t chunkSize = 0;
        try {
            chunkSize = std::stoul(sizeStr, nullptr, 16);
        } catch (...) {
            err = "invalid chunk size";
            return false;
        }

        if (chunkSize == 0) {
            // letzte Chunk: consume optional trailer headers until empty line
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.empty())
                    break;
            }
            return true;
        }

        // Lese exakt chunkSize Bytes
        std::string chunk;
        chunk.resize(chunkSize);
        stream.read(&chunk[0], static_cast<std::streamsize>(chunkSize));
        std::streamsize got = stream.gcount();
        if (static_cast<size_t>(got) != chunkSize) {
            err = "incomplete chunk data";
            return false;
        }

        // Body-Limit prüfen (nach dem Lesen, bevor wir ins out-Buffer gehen)
        if (maxBody > 0 && out.size() + chunkSize > maxBody) {
            tooLarge = true;
            err = "chunked body too large";
            // CRLF nach dem Chunk trotzdem sauber konsumieren:
            int c1 = stream.get();
            if (c1 == '\r') {
                int c2 = stream.get();
                (void)c2;
            } else if (c1 != '\n' && c1 != EOF) {
                // egal, wir brechen eh ab
            }
            return false;
        }

        out.append(chunk);

        // Nach dem Chunk muss ein CRLF kommen; versuchen sauber zu konsumieren
        // Lese zwei Zeichen; toleriere LF allein (robust gegen einige Sender).
        int c1 = stream.get();
        if (c1 == EOF) {
            err = "missing chunk terminator (EOF)";
            return false;
        }
        if (c1 == '\n') {
            // ok (lenient)
            continue;
        }
        if (c1 == '\r') {
            int c2 = stream.get();
            if (c2 == EOF) {
                err = "missing chunk terminator (EOF)";
                return false;
            }
            if (c2 == '\n')
                continue;
            // unexpected char after '\r'
            err = "invalid chunk terminator";
            return false;
        }
        // unexpected char (neither LF nor CR)
        err = "invalid chunk terminator";
        return false;
    }
    // unreachable
    return false;
}

// Request RequestParser::parse(const std::string& rawRequest, const LocationConfig& locationConfig, const ServerConfig& serverConfig)
// {
//     Request req;
//     std::istringstream stream(rawRequest);
//     std::string line;

//     // ------------------------------------------------------
//     // REQUEST LINE
//     // ------------------------------------------------------
//     if (!std::getline(stream, line))
//         return req; // empty request
//     if (!line.empty() && line.back() == '\r')
//         line.pop_back();
//     parseRequestLine(line, req);

//     // ------------------------------------------------------
//     // HEADERS
//     // ------------------------------------------------------
//     while (std::getline(stream, line))
//     {
//         if (!line.empty() && line.back() == '\r')
//             line.pop_back();
//         if (line.empty())
//             break;
//         parseHeaderLine(line, req);
//     }

//     // Connection handling
//     if (req.version == "HTTP/1.1")
//         req.keep_alive = !(req.headers.count("Connection")
//                            && req.headers["Connection"] == "close");
//     else if (req.version == "HTTP/1.0")
//         req.keep_alive = (req.headers.count("Connection")
//                           && req.headers["Connection"] == "keep-alive");

//     // ------------------------------------------------------
//     // READ BODY SIZE HEADERS
//     // ------------------------------------------------------
//     if (req.headers.count("Transfer-Encoding")
//         && req.headers["Transfer-Encoding"] == "chunked")
//     {
//         req.is_chunked = true;
//     }
//     else if (req.headers.count("Content-Length"))
//     {
//         try {
//             req.content_len = std::stoul(req.headers["Content-Length"]);
//         } catch (...) {
//             req.content_len = 0;
//         }
//     }

//     // ------------------------------------------------------
//     // READ BODY (chunked or normal)
//     // ------------------------------------------------------
//     if (req.is_chunked)
//     {
//         std::string err;
//         if (!decodeChunkedBody(stream, req.body, err))
//         {
//             std::cerr << "Chunked decode error: " << err << std::endl;
//             req.body.clear();
//             req.content_len = 0;
//         }
//         else
//         {
//             req.content_len = req.body.size();
//         }
//     }
//     else if (req.content_len > 0)
//     {
//         std::string body;
//         body.resize(req.content_len);
//         stream.read(&body[0], req.content_len);
//         body.resize(static_cast<size_t>(stream.gcount()));
//         req.body = body;
//     }
//     else
//     {
//         std::string rest;
//         std::getline(stream, rest, '\0');
//         req.body = rest;
//         req.content_len = req.body.size();
//     }

//     // ------------------------------------------------------
//     //  MAX BODY SIZE CHECK (LOCATION > SERVER)
//     // ------------------------------------------------------
//     const size_t maxBody =
//         (locationConfig.client_max_body_size > 0)
//         ? locationConfig.client_max_body_size
//         : serverConfig.client_max_body_size;

//     if (maxBody > 0 && req.content_len > maxBody)
//     {
//         req.error = 413;            // Payload Too Large
//         return req;                // Handler erstellt danach Error-Response
//     }

//     return req;
// }

// In RequestParser.cpp
bool RequestParser::parseHeaders(const std::string& rawHeaders, Request& req)
{
    std::istringstream stream(rawHeaders);
    std::string line;
    
    // Request-Line
    if (!std::getline(stream, line))
        return false;
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    parseRequestLine(line, req);
    
    // Headers
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break;
        parseHeaderLine(line, req);
    }
    
    // Connection handling
    if (req.version == "HTTP/1.1")
        req.keep_alive = !(req.headers.count("Connection") && 
                          req.headers["Connection"] == "close");
    else if (req.version == "HTTP/1.0")
        req.keep_alive = (req.headers.count("Connection") && 
                         req.headers["Connection"] == "keep-alive");
    
    return true;
}

bool RequestParser::parseBody(
    std::istringstream& stream,
    Request& req,
    const LocationConfig& locationConfig,
    const ServerConfig& serverConfig)
{
    // MAX BODY SIZE (LOCATION > SERVER)
    const size_t maxBody =
        (locationConfig.client_max_body_size > 0)
        ? locationConfig.client_max_body_size
        : serverConfig.client_max_body_size;

    // READ BODY SIZE HEADERS (bereits bekannt aus Headers)
    req.is_chunked = false;
    req.content_len = 0;

    if (req.headers.count("Transfer-Encoding") &&
        req.headers["Transfer-Encoding"] == "chunked") {
        req.is_chunked = true;
    } else if (req.headers.count("Content-Length")) {
        try {
            req.content_len = std::stoul(req.headers["Content-Length"]);
        } catch (...) {
            req.content_len = 0;
        }
    }

    // READ BODY (chunked or normal)
    if (req.is_chunked) {
        std::string err;
        bool tooLarge = false;
        if (!decodeChunkedBody(stream, req.body, err, maxBody, tooLarge)) {
            std::cerr << "Chunked decode error: " << err << std::endl;
            req.body.clear();
            req.content_len = 0;
            if (tooLarge && maxBody > 0) {
                req.error = 413; // Payload Too Large
            } else {
                req.error = 400; // Bad Request
            }
            return false;
        }
        req.content_len = req.body.size();
    } else if (req.content_len > 0) {
        std::string body;
        body.resize(req.content_len);
        stream.read(&body[0], static_cast<std::streamsize>(req.content_len));
        body.resize(static_cast<size_t>(stream.gcount()));
        req.body = body;
    } else {
        // Kein Content-Length & nicht chunked: lies einfach den Rest
        std::string rest;
        std::getline(stream, rest, '\0');
        req.body = rest;
        req.content_len = req.body.size();
    }

    // MAX BODY SIZE CHECK für nicht-chunked (chunked wurde schon im Decoder begrenzt)
    if (!req.is_chunked && maxBody > 0 && req.content_len > maxBody) {
        req.error = 413; // Payload Too Large
        return false;
    }

    return true;
}

static inline std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::map<std::string,std::string> parseCookieHeader(const std::string& header)
{
    std::map<std::string,std::string> out;
    size_t pos = 0;
    while (pos < header.size()) {
        // split by ';'
        size_t semi = header.find(';', pos);
        std::string pair = header.substr(pos, (semi==std::string::npos) ? std::string::npos : semi - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string k = trim(pair.substr(0, eq));
            std::string v = trim(pair.substr(eq + 1));
            out[k] = v;
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return out;
}

void RequestParser::parseRequestLine(const std::string& line, Request& req)
{
	std::istringstream ss(line);
	ss >> req.method >> req.path >> req.version;

	if (req.method.empty() || req.path.empty() || req.version.empty())
		std::cerr << "Invalid request line" << std::endl;
}

void RequestParser::parseHeaderLine(const std::string& line, Request& req)
{
	size_t pos = line.find(':');
	if (pos == std::string::npos)
		return;

	std::string key = line.substr(0, pos);
	std::string value = line.substr(pos + 1);


	if (!value.empty() && value[0] == ' ')
        value.erase(0, 1);

    if (key == "Cookie")
        req.cookies = parseCookieHeader(value);
    else
        req.headers[key] = value;
}
