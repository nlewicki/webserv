/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: mhummel <mhummel@student.42.fr>            +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2025/10/20 12:53:31 by mhummel           #+#    #+#             */
/*   Updated: 2025/10/20 13:15:37 by mhummel          ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "config.hpp"
#include <iostream>

void printConfig(const Config& config) {
    std::cout << "Global Defaults:\n";
    auto it = config.default_error_pages.find(404);
    if (it != config.default_error_pages.end()) {
        std::cout << "  Error Page 404: " << it->second << "\n";
    } else {
        std::cout << "  Error Page 404: Not set\n"; // Default, wenn nicht definiert
    }
    std::cout << "  Client Max Body Size: " << config.default_client_max_body_size << " bytes\n";

    for (size_t i = 0; i < config.servers.size(); ++i) {
        const ServerConfig& server = config.servers[i];
        std::cout << "Server " << i << ":\n";
        std::cout << "  Listen: " << server.listen_host << ":" << server.listen_port << "\n";
        std::cout << "  Server Name: " << server.server_name << "\n";
        std::cout << "  Locations:\n";
        for (size_t j = 0; j < server.locations.size(); ++j) {
            const LocationConfig& loc = server.locations[j];
            std::cout << "    Location " << loc.path << ":\n";
            std::cout << "      Root: " << loc.root << "\n";
            std::cout << "      Index: " << loc.index << "\n";
            std::cout << "      Autoindex: " << (loc.autoindex ? "on" : "off") << "\n";
            std::cout << "      Methods: ";
            for (size_t k = 0; k < loc.methods.size(); ++k) {
                std::cout << loc.methods[k] << (k < loc.methods.size() - 1 ? ", " : "\n");
            }
            std::cout << "      CGI: ";
            for (std::map<std::string, std::string>::const_iterator it = loc.cgi.begin(); it != loc.cgi.end(); ++it) {
                std::cout << it->first << " -> " << it->second << " ";
            }
            std::cout << "\n";
        }
    }
}

int main(int argc, char** argv) {
    Config config;
    try {
        std::string filename = (argc > 1) ? argv[1] : "test.conf";
        config.parse(filename);
        printConfig(config);
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (const std::out_of_range& e) {
        std::cerr << "Error: " << e.what() << " (Key 404 not found in error_pages)\n";
        return 1;
    }
    return 0;
}

