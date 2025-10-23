/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   config.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mhummel <mhummel@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/20 12:53:26 by mhummel           #+#    #+#             */
/*   Updated: 2025/10/20 12:53:27 by mhummel          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include <map>

// Struktur für Location-Konfiguration
struct LocationConfig {
	std::string path;                  // z.B. "/", "/cgi-bin"
	std::vector<std::string> methods;  // z.B. {"GET", "POST", "DELETE"}
	std::string root;                  // z.B. "/var/www"
	std::string index;                 // z.B. "index.html"
	bool autoindex;                    // z.B. true (on) oder false (off)
	std::map<std::string, std::string> cgi;  // z.B. {".php", "/usr/bin/php-cgi"}
	std::map<int, std::string> error_pages;  // Erbt von Server/Global
};

// Struktur für Server-Konfiguration
struct ServerConfig {
	std::string listen_host;  // z.B. "127.0.0.1"
	int listen_port;         // z.B. 80
	std::string server_name;  // z.B. "localhost"
	std::vector<LocationConfig> locations;
	std::map<int, std::string> error_pages;  // Erbt von Global
	size_t client_max_body_size;            // Erbt von Global
};

// Haupt-Konfigurationsklasse
class Config {
public:
	std::vector<ServerConfig> servers;
	std::map<int, std::string> default_error_pages;  // Globale Error-Pages
	size_t default_client_max_body_size;            // Globale Body-Size

	Config();  // Konstruktor mit Default-Werten
	void parse(const std::string& filename);  // Parsen der Config-Datei
	const std::vector<ServerConfig>& getServers() const { return servers; }
};

#endif
