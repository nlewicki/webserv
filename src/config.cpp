/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   config.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: nicolewicki <nicolewicki@student.42.fr>    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/20 12:53:20 by mhummel           #+#    #+#             */
/*   Updated: 2025/10/27 09:21:30 by nicolewicki      ###   ########.fr       */
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
void Config::parse_c(const std::string& filename) {
    std::ifstream file(filename.c_str());
    if (!file.is_open()) throw std::runtime_error("Cannot open config file: " + filename);

    std::vector<Context> contextStack;
    contextStack.push_back(GLOBAL); // Initialer GLOBAL-Kontext
    ServerConfig* currentServer = nullptr;
    LocationConfig* currentLocation = nullptr;

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        #ifdef DEBUG
        std::cerr << "Parsing line " << lineNum << ": '" << line << "' (Stack size: " << contextStack.size() << ")" << std::endl; // Erweiterte Debugging
        #endif
        if (line.empty() || line[0] == '#') continue;  // Ignoriere Kommentare und Leerzeilen

        // Handle Block-Ende
        if (line == "}") {
            if (contextStack.empty()) {
                #ifdef DEBUG
                std::cerr << "Warning: Extra } on line " << lineNum << ", ignoring" << std::endl;
                #endif
                continue;
            }
            Context closedContext = contextStack.back();
            contextStack.pop_back();
            #ifdef DEBUG
            std::cerr << "Popped " << (closedContext == LOCATION ? "LOCATION" : closedContext == SERVER ? "SERVER" : "GLOBAL") << ", Stack size now: " << contextStack.size() << std::endl;
            #endif
            if (closedContext == LOCATION) {
                currentLocation = nullptr;
                #ifdef DEBUG
                std::cerr << "Closed location block, context now: " << (contextStack.empty() ? "GLOBAL" : contextStack.back() == SERVER ? "SERVER" : contextStack.back() == LOCATION ? "LOCATION" : "GLOBAL") << std::endl;
                #endif
            } else if (closedContext == SERVER) {
                currentServer = nullptr;
                #ifdef DEBUG
                std::cerr << "Closed server block, context now: " << (contextStack.empty() ? "GLOBAL" : contextStack.back() == SERVER ? "SERVER" : contextStack.back() == LOCATION ? "LOCATION" : "GLOBAL") << std::endl;
                #endif
            } else if (closedContext == GLOBAL) {
                #ifdef DEBUG
                std::cerr << "Warning: Closing global context on line " << lineNum << ", ignoring" << std::endl;
                #endif
            }
            continue;
        }

        // Prüfe auf Block-Start
        size_t serverPos = line.find("server");
        size_t openBracePos = line.find('{');
        if (serverPos != std::string::npos && openBracePos != std::string::npos && serverPos < openBracePos) {
            if (contextStack.back() != GLOBAL) {
                throw std::runtime_error("Server block not allowed in this context on line " + std::to_string(lineNum));
            }
            servers.push_back(ServerConfig());
            currentServer = &servers.back();
            contextStack.push_back(SERVER);
            #ifdef DEBUG
            std::cerr << "Started server block on line " << lineNum << ", Stack size: " << contextStack.size() << std::endl;
            #endif
            continue;
        } else if (line.find("location") == 0 && openBracePos != std::string::npos) {
            if (contextStack.back() != SERVER) {
                throw std::runtime_error("Location block not allowed in this context on line " + std::to_string(lineNum));
            }
            if (!currentServer) {
                throw std::runtime_error("No current server on line " + std::to_string(lineNum));
            }
            currentServer->locations.push_back(LocationConfig());
            currentLocation = &currentServer->locations.back();
            size_t pathEnd = line.find('{');
            currentLocation->path = trim(line.substr(8, pathEnd - 8)); // "location " hat 9 Zeichen
            contextStack.push_back(LOCATION);
            #ifdef DEBUG
            std::cerr << "Started location block: " << currentLocation->path << " on line " << lineNum << ", Stack size: " << contextStack.size() << std::endl;
            #endif
            // Parse interne Direktiven
            std::string nextLine;
            while (std::getline(file, nextLine)) {
                lineNum++;
                nextLine = trim(nextLine);
                #ifdef DEBUG
                std::cerr << "Parsing inner line " << lineNum << ": '" << nextLine << "' (Stack size: " << contextStack.size() << ")" << std::endl;
                #endif
                if (nextLine.empty() || nextLine[0] == '#') continue;
                if (nextLine == "}") {
                    contextStack.pop_back(); // Schließe den location-Kontext
                    #ifdef DEBUG
                    std::cerr << "Closed location block on line " << lineNum << ", Stack size: " << contextStack.size() << std::endl;
                    #endif
                    break;
                }
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

    // Überprüfe den Stack-Zustand am Ende
    if (!contextStack.empty() && !(contextStack.size() == 1 && contextStack.back() == GLOBAL)) {
        #ifdef DEBUG
        std::cerr << "Warning: Unbalanced blocks at end of file, stack contains: ";
        for (size_t i = 0; i < contextStack.size(); ++i) {
            std::cerr << contextStack[i] << (i < contextStack.size() - 1 ? ", " : "\n");
        }
        std::cerr << "Assuming implicit closure" << std::endl;
        #endif
        while (!contextStack.empty()) {
            contextStack.pop_back();
            if (currentLocation) currentLocation = nullptr;
            if (currentServer) currentServer = nullptr;
        }
    } else {
        if (!contextStack.empty()) {
            contextStack.pop_back(); // Entferne den initialen GLOBAL-Kontext
            #ifdef DEBUG
            std::cerr << "Removed initial GLOBAL context, Stack size now: " << contextStack.size() << std::endl;
            #endif
        }
        #ifdef DEBUG
        std::cerr << "Stack is empty at end of file" << std::endl;
        #endif
    }

    // Setze Defaults nach dem Parsen
    if (servers.empty()) {
        // Default-Server, wenn keine Config
        servers.push_back(ServerConfig());
        currentServer = &servers.back();
        currentServer->listen_host = "127.0.0.1";
        currentServer->listen_port = 8080;
        currentServer->server_name = "default";
        currentServer->client_max_body_size = default_client_max_body_size;
        LocationConfig defaultLoc;
        defaultLoc.path = "/";
        defaultLoc.root = "./";
        defaultLoc.index = "index.html";
        defaultLoc.autoindex = false;
        defaultLoc.methods = {"GET", "POST", "DELETE"};
        currentServer->locations.push_back(defaultLoc);
    }
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

