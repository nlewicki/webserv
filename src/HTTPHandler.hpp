#ifndef HTTPHANDLER_HPP
# define HTTPHANDLER_HPP

#include <string>
#include <map>

struct Request
{
	std::string method;
	std::string uri;
	std::string http_version;
	std::map<std::string, std::string> headers;
	std::string body;
};


#endif