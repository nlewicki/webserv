/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   config.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mhummel <mhummel@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/20 12:53:20 by mhummel           #+#    #+#             */
/*   Updated: 2025/10/20 13:29:46 by mhummel          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "config.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>  // Für std::remove_if
#include <cctype>     // Für std::isspace
#include <stdexcept>  // Für std::runtime_error
#include <iostream>   // Für Debug-Ausgaben

// Hilfsfunktion: Entfernt führende und nachfolgende Whitespaces
std::string trim(const std::string& str) {
    std::string s = str;
    s.erase(0, s.find_first_not_of(" \t\n\r\f\v"));  // Left Trim
    s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1);  // Right Trim
    return s;
}

// Hilfsfunktion: Parst Größenangaben wie 2M oder 1K
size_t parseSize(const std::string& sizeStr) {
    size_t size = std::atoi(sizeStr.c_str());
    if (sizeStr.find('M') != std::string::npos) size *= 1024 * 1024;
    else if (sizeStr.find('K') != std::string::npos) size *= 1024;
    return size;
}

// Enum für Kontext-Tracking
enum Context { GLOBAL, SERVER, LOCATION };

// Konstruktor mit Default-Werten
Config::Config() : default_client_max_body_size(1048576) {}

// Haupt-Parsing-Funktion
void Config::parse(const std::string& filename) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) throw std::runtime_error("Cannot open config file: " + filename);

    std::vector<Context> contextStack;
    contextStack.push_back(GLOBAL);
    ServerConfig* currentServer = nullptr;
    LocationConfig* currentLocation = nullptr;

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        std::cerr << "Parsing line " << lineNum << ": '" << line << "'" << std::endl; // Debugging mit Anführungszeichen
        if (line.empty() || line[0] == '#') continue;  // Ignoriere Kommentare und Leerzeilen

        // Handle Block-Ende
        if (line == "}") {
            if (contextStack.empty()) {
                std::cerr << "Warning: Extra } on line " << lineNum << ", ignoring" << std::endl;
                continue;
            }
            Context closedContext = contextStack.back();
            contextStack.pop_back();
            if (!contextStack.empty() && closedContext == LOCATION) {
                currentLocation = nullptr;
            } else if (!contextStack.empty() && closedContext == SERVER) {
                currentServer = nullptr; // Setze nur, wenn der server-Block vollständig geschlossen wird
            }
            std::cerr << "Closed block, context now: " << (contextStack.empty() ? "GLOBAL" : contextStack.back() == SERVER ? "SERVER" : "LOCATION") << std::endl;
            continue;
        }

        // Prüfe auf Block-Start (robuster mit Whitespaces)
        size_t serverPos = line.find("server");
        size_t openBracePos = line.find('{');
        if (serverPos != std::string::npos && openBracePos != std::string::npos && serverPos < openBracePos) {
            if (contextStack.back() != GLOBAL) {
                throw std::runtime_error("Server block not allowed in this context on line " + std::to_string(lineNum));
            }
            servers.push_back(ServerConfig());
            currentServer = &servers.back();
            contextStack.push_back(SERVER);
            std::cerr << "Started server block on line " << lineNum << std::endl;
            continue;
        } else if (line.find("location") == 0 && openBracePos != std::string::npos) {
            if (contextStack.back() != SERVER) throw std::runtime_error("Location block not allowed in this context on line " + std::to_string(lineNum));
            if (!currentServer) throw std::runtime_error("No current server on line " + std::to_string(lineNum));
            currentServer->locations.push_back(LocationConfig());
            currentLocation = &currentServer->locations.back();
            // Extrahiere den Pfad vor dem {
            size_t pathEnd = line.find('{');
            currentLocation->path = trim(line.substr(8, pathEnd - 8)); // "location " hat 9 Zeichen
            contextStack.push_back(LOCATION);
            std::cerr << "Started location block: " << currentLocation->path << " on line " << lineNum << std::endl;
            // Parse interne Direktiven sofort
            std::string nextLine;
            while (std::getline(file, nextLine) && trim(nextLine) != "}") {
                lineNum++;
                nextLine = trim(nextLine);
                std::cerr << "Parsing inner line " << lineNum << ": '" << nextLine << "'" << std::endl;
                if (nextLine.empty() || nextLine[0] == '#') continue;
                size_t innerSemiPos = nextLine.find_last_of(';');
                if (innerSemiPos == std::string::npos) {
                    throw std::runtime_error("Missing ; on line " + std::to_string(lineNum));
                }
                std::string innerDirective = trim(nextLine.substr(0, innerSemiPos));
                std::istringstream iss(innerDirective);
                std::string key;
                iss >> key;
                std::vector<std::string> params;
                std::string param;
                while (iss >> param) params.push_back(param);
                if (key == "root" && !params.empty()) {
                    currentLocation->root = params[0];
                } else if (key == "index" && !params.empty()) {
                    currentLocation->index = params[0];  // Erstes, ignoriere mehr
                } else if (key == "autoindex" && !params.empty()) {
                    currentLocation->autoindex = (params[0] == "on");
                } else if (key == "methods" && !params.empty()) {
                    currentLocation->methods = params;
                } else if (key == "cgi" && params.size() >= 2) {
                    currentLocation->cgi[params[0]] = params[1];
                } else {
                    throw std::runtime_error("Unknown directive: " + key + " on line " + std::to_string(lineNum));
                }
            }
            if (trim(nextLine) == "}") {
                lineNum++;
                std::cerr << "Closed location block on line " << lineNum << std::endl;
            }
            continue;
        }

        // Parse Direktiven basierend auf globalem oder server-Kontext
        size_t semiPos = line.find_last_of(';');
        if (semiPos == std::string::npos) {
            throw std::runtime_error("Missing ; on line " + std::to_string(lineNum));
        }
        std::string directive = trim(line.substr(0, semiPos));

        std::istringstream iss(directive);
        std::string key;
        iss >> key;
        std::vector<std::string> params;
        std::string param;
        while (iss >> param) params.push_back(param);

        Context ctx = contextStack.back();
        if (ctx == SERVER && currentServer) {
            if (key == "listen" && !params.empty()) {
                size_t colonPos = params[0].find(':');
                if (colonPos != std::string::npos) {
                    currentServer->listen_host = params[0].substr(0, colonPos);
                    currentServer->listen_port = std::atoi(params[0].substr(colonPos + 1).c_str());
                } else {
                    currentServer->listen_port = std::atoi(params[0].c_str());
                    currentServer->listen_host = "*";  // Default Wildcard
                }
            } else if (key == "server_name" && !params.empty()) {
                currentServer->server_name = params[0];  // Erstes, ignoriere mehr
            } else if (key == "error_page" && !params.empty()) {
                if (params.size() < 2) throw std::runtime_error("Invalid error_page directive on line " + std::to_string(lineNum));
                int code = std::atoi(params[0].c_str());
                std::string path = params[1];
                currentServer->error_pages[code] = path;
            } else if (key == "client_max_body_size" && !params.empty()) {
                currentServer->client_max_body_size = parseSize(params[0]);
            }
        } else if (ctx == GLOBAL) {
            if (key == "error_page" && !params.empty()) {
                if (params.size() < 2) throw std::runtime_error("Invalid error_page directive on line " + std::to_string(lineNum));
                int code = std::atoi(params[0].c_str());
                std::string path = params[1];
                default_error_pages[code] = path;
            } else if (key == "client_max_body_size" && !params.empty()) {
                default_client_max_body_size = parseSize(params[0]);
            }
        } else {
            throw std::runtime_error("Unknown directive: " + key + " on line " + std::to_string(lineNum));
        }
    }

    // Toleranter Check für unbalancierte Blöcke
    if (!contextStack.empty()) {
        std::cerr << "Warning: Unbalanced blocks at end of file, assuming implicit closure" << std::endl;
        while (!contextStack.empty()) {
            contextStack.pop_back();
            if (currentLocation) currentLocation = nullptr;
            if (currentServer) currentServer = nullptr;
        }
    }

    // Setze Defaults nach dem Parsen
    for (std::vector<ServerConfig>::iterator it = servers.begin(); it != servers.end(); ++it) {
        if (it->listen_port == 0) it->listen_port = 80;  // Default-Port
        if (it->client_max_body_size == 0) it->client_max_body_size = default_client_max_body_size;
        if (it->error_pages.empty()) it->error_pages = default_error_pages;
        for (std::vector<LocationConfig>::iterator locIt = it->locations.begin(); locIt != it->locations.end(); ++locIt) {
            if (locIt->index.empty()) locIt->index = "index.html";
            if (locIt->methods.empty()) locIt->methods.push_back("GET"), locIt->methods.push_back("POST"), locIt->methods.push_back("DELETE");
            if (locIt->error_pages.empty()) locIt->error_pages = it->error_pages;
            if (!locIt->autoindex) locIt->autoindex = false;  // Explicit setzen
        }
    }
}

