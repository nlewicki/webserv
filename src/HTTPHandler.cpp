/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HTTPHandler.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: leokubler <leokubler@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:22 by mhummel           #+#    #+#             */
/*   Updated: 2025/10/28 17:03:44 by leokubler        ###   ########.fr       */
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

	
	// Request Line
	if (std::getline(stream, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		parseRequestLine(line, req);
    }

	// Header line -> ' '
	while (std::getline(stream, line))
	{
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            break;
        parseHeaderLine(line, req);
    }
	
	if (req.version == "HTTP/1.1")
	{
		req.keep_alive = !(req.headers.count("Connection") && req.headers["Connection"] == "close");
	}
	else if (req.version == "HTTP/1.0")
	{
		req.keep_alive = (req.headers.count("Connection") && req.headers["Connection"] == "keep-alive");
	}
	else
	{
		// Ungültige Version
		std::cerr << "Invalid HTTP version" << std::endl;
	}
	
	if (req.headers.count("Transfer-Encoding") && req.headers["Transfer-Encoding"] == "chunked")
	{
		req.is_chunked = true;
	}
	else if (req.headers.count("Content-Length"))
	{
		req.content_len = std::stoul(req.headers["Content-Length"]);
	}
	else
	{
		// Ungültige Content-Length
		std::cerr << "Invalid Content-Length" << std::endl;
	}

	// Body
	std::string body;
    while (std::getline(stream, line))
        body += line + "\n";
    if (!body.empty() && body.back() == '\n')
        body.pop_back();
    req.body = body;

    return req;
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

    req.headers[key] = value;
}
