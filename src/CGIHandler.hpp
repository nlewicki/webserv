/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   CGIHandler.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mhummel <mhummel@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/21 09:27:20 by mhummel           #+#    #+#             */
/*   Updated: 2025/10/21 09:27:21 by mhummel          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CGI_HANDLER_HPP
#define CGI_HANDLER_HPP

#include "HTTPHandler.hpp"
#include "Response.hpp"
#include <string>
#include <map>

class CGIHandler
{
public:
	CGIHandler();
	~CGIHandler();

	// Führt das CGI-Script aus und gibt HTTP-Response zurück
	Response execute(const Request& req);

private:
	// Hilfsfunktionen
	std::map<std::string, std::string> buildEnv(const Request& req, const std::string& scriptPath);
	std::string runCGI(const std::string& scriptPath, const std::map<std::string, std::string>& env, const std::string& body);
};

#endif
