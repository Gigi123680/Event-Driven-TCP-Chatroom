#include <iostream>
#include <string>

#include "client/client.hpp"
#include "server/server.hpp"

int main() {
  while (true) {
    std::cout << "Welcome to the chatroom!" << std::endl;
    std::cout << "Please select a mode:\n1. Server\n2. Client" << std::endl;
    std::cout << "> ";
    int mode;
    std::cin >> mode;

    if (mode == 1) {
      server_init();
      break;
    } else if (mode == 2) {
      client_init();
      break;
    } else {
      std::cout << "Invalid mode selected." << std::endl;
    }
  }
}
