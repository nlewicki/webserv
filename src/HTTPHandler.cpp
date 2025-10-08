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

	// Reqeust Line
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

	// Body
	std::string body;
    while (std::getline(stream, line))
        body += line + "\n";
    if (!body.empty() && body.back() == '\n')
        body.pop_back();
    req.body = body;

    return req;
}