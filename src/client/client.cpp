#include <arpa/inet.h>
#include <iostream>
#include <limits>
#include <netinet/in.h>
#include <string>

#include "../connection/connection.hpp"
#include "client.hpp"

static Client *client;

void client_event_loop();

/**
 * Initialize a client instance, and set up the connection to the server.
 */
void client_init() {
  std::cout << "Initializing client..." << std::endl;
  std::cout << "Enter your name: ";
  std::string name;
  std::cin >> name;
  std::cout << "Your name is: " << name << std::endl;
  client = new Client();
  client->name = name;

  std::string server_ip;
  sockaddr_in server_addr{};
  while (true) {
    std::cout << "Enter server IP address: ";
    if (!(std::cin >> server_ip)) {
      std::cerr << "Error reading server IP address." << std::endl;
      std::cin.clear(); // Clear the error flag
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      continue;
    }

    // convert to binary form
    server_addr = {};
    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) == 1) {
      break;
    }

    std::cerr << "Invalid IPv4 address." << std::endl;
  }

  int server_port;
  while (true) {
    std::cout << "Enter server port: ";
    if (!(std::cin >> server_port)) {
      std::cerr << "Error reading server port." << std::endl;
      std::cin.clear(); // Clear the error flag
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      continue;
    }

    if (server_port >= 1 && server_port <= 65535) {
      break;
    }

    std::cerr << "Port must be between 1 and 65535." << std::endl;
  }
  server_addr.sin_port = htons(static_cast<uint16_t>(server_port));

  Connection *connection = client_connect_to_server(&server_addr);
  if (connection == nullptr) {
    std::cerr << "Failed to start connection to server.\nExiting..."
              << std::endl;
    return;
  }
  client->conn = connection;
  std::cout << "Connection setup started." << std::endl;

  client_event_loop();
}

/**
 * The main event loop for the client.
 */
void client_event_loop() {}
