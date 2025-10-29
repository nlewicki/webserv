/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HTTPHandler.hpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: leokubler <leokubler@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:24 by mhummel           #+#    #+#             */
/*   Updated: 2025/10/22 13:39:52 by leokubler        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTPHANDLER_HPP
# define HTTPHANDLER_HPP

#include <string>
#include <map>
#include "config.hpp"

struct Request
{
	// Verbindungsdaten
	bool keep_alive = false; // aus Version+Header abgeleitet
	int conn_fd = -1; // -1 heist, keine verbindung
	
	// Request-Daten
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
