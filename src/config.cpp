/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   config.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mhummel <mhummel@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/20 12:53:20 by mhummel           #+#    #+#             */
/*   Updated: 2025/11/27 10:40:54 by mhummel          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "config.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>  // Für std::remove_if
#include <cctype>     // Für std::isspace
#include <stdexcept>  // Für std::runtime_error
#include <iostream>   // Für Debug-Ausgaben

// ====================================================================
// Hilfsfunktionen
// ====================================================================

static std::string trim(const std::string& str) {
	std::string s = str;
	s.erase(0, s.find_first_not_of(" \t\n\r\f\v"));
	s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1);
	return s;
}

static size_t parseSize(const std::string& sizeStr) {
	size_t size = std::atoi(sizeStr.c_str());
	if (sizeStr.find('M') != std::string::npos) size *= 1024 * 1024;
	else if (sizeStr.find('K') != std::string::npos) size *= 1024;
	return size;
}

// ====================================================================
// Config Member-Funktionen (müssen inline oder im .cpp sein!)
// ====================================================================

Config::Config() : default_client_max_body_size(1048576) {}

void Config::handleClosingBrace(std::vector<Context>& stack,
								ServerConfig*& currentServer,
								LocationConfig*& currentLocation) {
	if (stack.empty()) return;
	Context ctx = stack.back();
	stack.pop_back();
	if (ctx == LOCATION) currentLocation = nullptr;
	else if (ctx == SERVER) currentServer = nullptr;
}

void Config::parseServerBlock(std::vector<ServerConfig>& servers,
							std::vector<Context>& stack) {
	if (stack.back() != GLOBAL)
		throw std::runtime_error("Server block not allowed here");
	servers.push_back(ServerConfig());
	stack.push_back(SERVER);
}

void Config::parseLocationBlock(std::ifstream& file, int& lineNum,
								std::vector<Context>& stack,
								ServerConfig* currentServer,
								LocationConfig*& currentLocation,
								const std::string& locationLine) {
	if (stack.back() != SERVER || !currentServer)
		throw std::runtime_error("Location block not allowed here");

	size_t bracePos = locationLine.find('{');
	if (bracePos == std::string::npos)
		throw std::runtime_error("Missing { in location");

	currentServer->locations.push_back(LocationConfig());
	currentLocation = &currentServer->locations.back();
	currentLocation->path = trim(locationLine.substr(8, bracePos - 8));
	stack.push_back(LOCATION);

	std::string inner;
	while (std::getline(file, inner)) {
		lineNum++;
		inner = trim(inner);
		if (inner.empty() || inner[0] == '#') continue;
		if (inner == "}") {
			stack.pop_back();
			currentLocation = nullptr;
			break;
		}

		size_t semi = inner.find_last_of(';');
		if (semi == std::string::npos)
			throw std::runtime_error("Missing ;");

		std::string dir = trim(inner.substr(0, semi));
		std::istringstream iss(dir);
		std::string key; iss >> key;
		std::vector<std::string> params;
		std::string p;
		while (iss >> p) params.push_back(p);

		if (key == "root" && !params.empty()) currentLocation->root = params[0];
		else if (key == "index" && !params.empty()) currentLocation->index = params[0];
		else if (key == "autoindex" && !params.empty()) currentLocation->autoindex = (params[0] == "on");
		else if (key == "allow_methods" && !params.empty()) currentLocation->methods = params;
		else if (key == "cgi" && params.size() >= 2) currentLocation->cgi[params[0]] = params[1];
		else if (key == "data_store" && !params.empty()) currentLocation->data_store = params[0];
		else if (key == "client_max_body_size" && !params.empty()) {
    currentLocation->client_max_body_size = parseSize(params[0]);
    currentLocation->has_client_max_body_size = true;
}
		else throw std::runtime_error("Unknown directive: " + key);
	}
}

void Config::resolveVariables() {
	for (auto& server : servers) {
		for (auto& loc : server.locations) {
			if (loc.data_store.empty()) continue;
			std::string resolved = loc.data_store;
			for (const auto& [var, val] : variables) {
				std::string ph = "$(" + var + ")";
				size_t pos;
				while ((pos = resolved.find(ph)) != std::string::npos) {
					resolved.replace(pos, ph.length(), val);
				}
			}
			loc.data_store = resolved;
		}
	}
}

void Config::parse_c(const std::string& filename) {
	std::ifstream file(filename.c_str());
	if (!file.is_open()) throw std::runtime_error("Cannot open config file: " + filename);

	std::vector<Context> contextStack{GLOBAL};
	ServerConfig* currentServer = nullptr;
	LocationConfig* currentLocation = nullptr;

	std::string line;
	int lineNum = 0;

	while (std::getline(file, line)) {
		lineNum++;
		line = trim(line);
		if (line.empty() || line[0] == '#') continue;

		if (line == "}") {
			handleClosingBrace(contextStack, currentServer, currentLocation);
			continue;
		}

		// === SERVER BLOCK ===
		if (line.find("server") == 0 && line.find('{') != std::string::npos) {
			parseServerBlock(servers, contextStack);
			currentServer = &servers.back();
			continue;
		}

		// === LOCATION BLOCK – jetzt robust! ===
		if (line.find("location ") == 0) {
			size_t bracePos = line.find('{');
			std::string locationLine = line;

			// Wenn kein { in dieser Zeile → nächste Zeile lesen
			if (bracePos == std::string::npos) {
				std::string nextLine;
				if (!std::getline(file, nextLine)) {
					throw std::runtime_error("Unexpected end of file after location");
				}
				lineNum++;
				nextLine = trim(nextLine);
				if (nextLine.find('{') == std::string::npos) {
					throw std::runtime_error("Missing { after location");
				}
				locationLine += " " + nextLine;  // Zusammenfügen
			}

			// Jetzt parseLocationBlock aufrufen
			// Aber: file-Zeiger ist schon richtig (erste Direktive kommt als nächstes)
			parseLocationBlock(file, lineNum, contextStack, currentServer, currentLocation, locationLine);
			continue;
		}

		// === NORMALE DIREKTIVEN (global/server) ===
		size_t semi = line.find_last_of(';');
		if (semi == std::string::npos)
			throw std::runtime_error("Missing ; on line " + std::to_string(lineNum));

		std::string directive = trim(line.substr(0, semi));
		std::istringstream iss(directive);
		std::string key; iss >> key;
		std::vector<std::string> params;
		std::string p;
		while (iss >> p) params.push_back(p);

		if (contextStack.back() == GLOBAL) {
			if (key == "error_page" && params.size() >= 2)
				default_error_pages[std::atoi(params[0].c_str())] = params[1];
			else if (key == "client_max_body_size" && !params.empty())
				default_client_max_body_size = parseSize(params[0]);
			else if (key == "data_dir" && !params.empty())
				variables["data_dir"] = params[0];
		}
		else if (contextStack.back() == SERVER && currentServer) {
			if (key == "listen" && !params.empty()) {
				size_t colon = params[0].find(':');
				currentServer->listen_host = (colon == std::string::npos) ? "0.0.0.0" : params[0].substr(0, colon);
				currentServer->listen_port = std::atoi(params[0].substr(colon + 1).c_str());
			}
			else if (key == "server_name" && !params.empty())
				currentServer->server_name = params[0];
		}
	}

	resolveVariables();
	// ────────────────────── VALIDIERUNG AM ENDE ──────────────────────
	if (servers.empty()) {
		throw std::runtime_error("No 'server {}' block found in config file");
	}

	for (const auto& srv : servers) {
		if (srv.listen_port == 0) {
			throw std::runtime_error("A server block is missing the 'listen' directive");
		}
		if (srv.locations.empty()) {
			throw std::runtime_error("A server block has no location blocks");
		}
	}
}
