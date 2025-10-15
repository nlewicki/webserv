#ifndef HTTPHANDLER_HPP
# define HTTPHANDLER_HPP

#include <string>
#include <map>

struct Request
{
	std::string method;
	std::string path;
	std::string version;
	std::string query;
	std::map<std::string, std::string> headers;
	std::string body;
};

class RequestParser
{
	public:
		RequestParser();
		~RequestParser();

		Request parse(const std::string& rawRequest);

	private:
		void parseRequestLine(const std::string& line, Request& req);
		void parseHeaderLine(const std::string& line, Request& req);
};
#endif