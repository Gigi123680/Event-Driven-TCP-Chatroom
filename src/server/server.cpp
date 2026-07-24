#include <iostream>
#include <limits>
#include <string>

#include "server.hpp"

void server_init() {
  std::cout << "Server mode selected." << std::endl;
  std::cout << "Enter your username: ";
  std::string username;
  std::cin >> username;
  std::cout << "Your username is: " << username << std::endl;

  int port;
  while (true) {
    std::cout << "Enter the port number to listen on: ";
    if (std::cin >> port) {
      break;
    }

    std::cout << "Invalid input. Please enter an integer." << std::endl;
    std::cin.clear(); // Clear the error flag
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  std::cout << "Port number is: " << port << std::endl;

  std::cout << "Initializing server instance..." << std::endl;
}
