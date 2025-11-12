/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HTTPHandler.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: leokubler <leokubler@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:22 by mhummel           #+#    #+#             */
/*   Updated: 2025/11/12 11:52:10 by leokubler        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "HTTPHandler.hpp"
#include <iostream>
#include <sstream>

RequestParser::RequestParser() {};

RequestParser::~RequestParser() {};

Request RequestParser::parse(const std::string& rawRequest)
{
    Request req;
    std::istringstream stream(rawRequest);
    std::string line;

    // Read and parse the request-line once
    if (!std::getline(stream, line))
        return req; // empty request
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    parseRequestLine(line, req);

    // Read headers
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break;
        parseHeaderLine(line, req);
    }

    // Connection / keep-alive logic
    if (req.version == "HTTP/1.1")
        req.keep_alive = !(req.headers.count("Connection") && req.headers["Connection"] == "close");
    else if (req.version == "HTTP/1.0")
        req.keep_alive = (req.headers.count("Connection") && req.headers["Connection"] == "keep-alive");
    else
        std::cerr << "Invalid HTTP version" << std::endl;

    // Transfer-Encoding / Content-Length
    if (req.headers.count("Transfer-Encoding") && req.headers["Transfer-Encoding"] == "chunked")
    {
        req.is_chunked = true;
    }
    else if (req.headers.count("Content-Length"))
    {
        try {
            req.content_len = std::stoul(req.headers["Content-Length"]);
        } catch (const std::exception& e) {
            std::cerr << "Invalid Content-Length: " << e.what() << std::endl;
            req.content_len = 0;
        }
    }
    // it's valid to have neither header (no body)

    // Body: prefer exact Content-Length when provided
    if (req.is_chunked)
    {
        // chunked decoding not implemented here; leave body empty or implement later
    }
    else if (req.content_len > 0)
    {
        std::string body;
        body.resize(req.content_len);
        stream.read(&body[0], req.content_len);
        std::streamsize actuallyRead = stream.gcount();
        body.resize(static_cast<size_t>(actuallyRead));
        req.body = body;
    }
    else
    {
        // read any remaining data
        std::string rest;
        std::getline(stream, rest, '\0');
        req.body = rest;
    }

    return req;
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
