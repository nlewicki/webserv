/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CGIHandler.cpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mhummel <mhummel@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:14 by mhummel           #+#    #+#             */
/*   Updated: 2025/10/21 09:40:37 by mhummel          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "CGIHandler.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <string.h>

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

std::map<std::string, std::string> CGIHandler::buildEnv(const Request& req, const std::string& scriptPath)
{
	std::map<std::string, std::string> env;
	env["GATEWAY_INTERFACE"] = "CGI/1.1";
	env["REQUEST_METHOD"] = req.method;
	env["SCRIPT_FILENAME"] = scriptPath;
	env["SCRIPT_NAME"] = req.path;
	env["QUERY_STRING"] = req.query;
	env["CONTENT_LENGTH"] = std::to_string(req.body.size());
	env["CONTENT_TYPE"] = "text/plain";
	env["SERVER_PROTOCOL"] = "HTTP/1.1";
	env["SERVER_SOFTWARE"] = "webserv/1.0";
	return env;
}

std::string CGIHandler::runCGI(const std::string& scriptPath, const std::map<std::string, std::string>& env, const std::string& body)
{
	int pipeIn[2];
	int pipeOut[2];

	if (pipe(pipeIn) < 0 || pipe(pipeOut) < 0)
	{
		perror("pipe");
		return "<h1>CGI pipe error</h1>";
	}

	pid_t pid = fork();

	if (pid == 0)
	{
		dup2(pipeIn[0], STDIN_FILENO);
		dup2(pipeOut[1], STDOUT_FILENO);
		close(pipeIn[1]);
		close(pipeOut[0]);

		// envs bauen
		char* envp[env.size() + 1];
		int i = 0;
		for (std::map<std::string, std::string>::const_iterator it = env.begin(); it != env.end(); ++it)
		{
			std::string entry = it->first + "=" + it->second;
			envp[i++] = strdup(entry.c_str());
		}
		envp[i] = NULL;

		// argv bauen
		char* argv[2];
		argv[0] = const_cast<char*>(scriptPath.c_str());
		argv[1] = NULL;

		execve(scriptPath.c_str(), argv, envp);
		perror("execve");
		exit(1);
	}
	else if (pid > 0)
	{
		close(pipeIn[0]);
		close(pipeOut[1]);

		// schreibe Request-Body an CGI
		if (!body.empty())
			write(pipeIn[1], body.c_str(), body.size());
		close(pipeIn[1]);

		// lese Ausgabe vom CGI
		std::ostringstream output;
		char buffer[4096];
		ssize_t bytes;
		while ((bytes = read(pipeOut[0], buffer, sizeof(buffer))) > 0)
			output.write(buffer, bytes);
		close(pipeOut[0]);

		waitpid(pid, NULL, 0);
		return output.str();
	}
	else
	{
		perror("fork");
		return "<h1>CGI fork error</h1>";
	}
}
