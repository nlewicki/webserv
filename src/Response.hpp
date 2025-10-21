/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Response.hpp                                       :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mhummel <mhummel@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:34 by mhummel           #+#    #+#             */
/*   Updated: 2025/10/21 09:27:35 by mhummel          ###   ########.fr       */
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

	std::string toString() const;
};

class ResponseHandler
{
	public:
		ResponseHandler();
		~ResponseHandler();

		Response handleRequest(const Request& req);

	private:
		std::string getStatusMessage(int code);
		std::string readFile(const std::string& path);
		bool fileExists(const std::string& path);
};

#endif
