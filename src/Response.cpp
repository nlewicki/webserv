#include "Response.hpp"
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <iostream>

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

Response handleRequest(const Request& req)
{
	Response res;

	// default headers
	res.headers["Server"] = "webserv/1.0";
    res.headers["Connection"] = "close";
    res.headers["Content-Type"] = "text/html";

	if (req.method == "GET")
	{
		std::string path = "." + req.path;
	}
	else if (req.method == "POST")
	{
		res.statusCode = 200;
		res.reasonPhrase = getStatusMessage(200);
		res.body = "<h1>POST received!</h1><p>" + req.body + "</p>";
	}
	else if (req.method == "DELETE")
	{

	}
	else
	{
		res.statusCode = 405;
		res.reasonPhrase = getStatusMessage(405);
        res.body = "<h1>405 Method Not Allowed</h1>";
	}
	res.headers ["Content-Length"] = std::to_string(res.body.size());
	return res;
}

std::string getStatusMessage(int code)
{
	switch (code)
	{
		case 200: return "OK";
		case 404: return "Not Found";
		case 405: return "Method not Allowed";
		default : return "Unkown";
	}
}

std::string readFile(const std::string& path)
{
	std::ifstream file(path.c_str());
	if (!file.is_open())
		return "<h1>Error opening file</h1>";;
	
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

bool fileExists(const std::string& path)
{
	struct stat buf;
	return (stat(path.c_str(), &buf) == 0);
}