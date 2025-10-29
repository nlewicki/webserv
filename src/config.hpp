/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   config.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mhummel <mhummel@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/20 12:53:26 by mhummel           #+#    #+#             */
/*   Updated: 2025/10/29 12:49:02 by mhummel          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <vector>
#include <map>

// webserv/
// ├── src/                     ← Dein Code (main.cpp, config.cpp)
// │   ├── main.cpp
// │   ├── config.cpp
// │   └── ...
// ├── include/                 ← Header
// │   ├── ...
// │   └── ...
// ├── config/                  ← Config-Datei
// │   └── webserv.conf         ← webserv.conf
// ├── html/                    ← Startseite
// ├── cgi-bin/                 ← CGI
// ├── data/                    ← Blog-Daten
// └── errors/                  ← 404

// In deinem Handler (z. B. ResponseHandler)
// std::string indexPath = config.root + "/" + config.index;        // ./html/index.html
// std::string cgiPath   = config.cgi_dir + "/hello.py";            // ./cgi-bin/hello.py
// std::string errorPath = config.error_dir + "/404.html";          // ./errors/404.html
// std::string dataPath  = config.data_store;                       // /var/www/data/posts.json

// Struktur für Location-Konfiguration
struct LocationConfig {
	std::string path;                  // z.B. "/""
	std::string root;                  // z.B. """
	std::string index;                 // z.B. "index.html"
	bool autoindex;                    // z.B. true (on) oder false (off)
	std::vector<std::string> methods;  // z.B. {"GET", "POST", "DELETE"}
	std::map<std::string, std::string> cgi;  // z.B. {".php", "/usr/bin/php-cgi"}
	std::map<int, std::string> error_pages;  // Erbt von Server/Global
	std::string cgi_dir;        // z.B. "./cgi-bin"
	std::string error_dir;      // z.B. "./errors"
	std::string data_dir;       // z.B. "./data"
	std::string data_store;     // z.B. "$(data_dir)/posts.json"
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
	std::map<std::string, std::string> variables;   // z.B. {"data_dir", "/var/www/data"}

	Config();  // Konstruktor mit Default-Werten
	void parse_c(const std::string& filename);  // Parsen der Config-Datei
	const std::vector<ServerConfig>& getServers() const { return servers; }
};

#endif
