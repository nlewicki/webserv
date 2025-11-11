/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Response.hpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: leokubler <leokubler@student.42.fr>        +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:34 by mhummel           #+#    #+#             */
/*   Updated: 2025/11/11 10:01:35 by leokubler        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef RESPONSE_HPP
# define RESPONSE_HPP

#include <string>
#include <map>
#include "HTTPHandler.hpp"

struct Response
{
	int statusCode;
	std::string reasonPhrase;
	std::map<std::string, std::string> headers;
	std::string body;
	bool keep_alive = false;
	std::vector<std::string> set_cookies;

	std::string toString() const;
	void setCookie(const std::string& name, const std::string& value, const std::string& path = "/", int maxAge = -1, bool httpOnly = false,
                   const std::string& sameSite = "");
};

class ResponseHandler
{
	public:
		ResponseHandler();
		~ResponseHandler();

		Response handleRequest(const Request& req, const LocationConfig& config);

	private:
		std::string getStatusMessage(int code);
		std::string readFile(const std::string& path);
		bool fileExists(const std::string& path);
};

#endif
