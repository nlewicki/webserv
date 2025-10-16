#include "HTTPHandler.hpp"
#include "Response.hpp"
#include <iostream>

int main() {
	Request req;
	req.method = "GET";
	req.path = "/test.py";
	req.body = "";

	ResponseHandler handler;
	Response res = handler.handleRequest(req);
	std::cout << res.toString() << std::endl;

}
