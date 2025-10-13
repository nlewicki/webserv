#include <fcntl.h>
#include <sstream>
#include <iostream>

CGIHandler::CGIHandler() {}
CGIHandler::~CGIHandler() {}

Response CGIHandler::execute(const Request& req)
{
	Response res;
	std::string scriptPath = "." + req.path;

	// baue Umgebungsvariablen
	std::map<std::string, std::string> env = buildEnv(req, scriptPath);

	// führe Script aus
	std::string output = runCGI(scriptPath, env, req.body);

	// baue HTTP-Response
	res.statusCode = 200;
	res.reasonPhrase = "OK";
	res.headers["Content-Type"] = "text/html";
	res.body = output;
	res.headers["Content-Length"] = std::to_string(res.body.size());

	return res;
}