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

}

std::string getStatusMessage(int code)
{

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