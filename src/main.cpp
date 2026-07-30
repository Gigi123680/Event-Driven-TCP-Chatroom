#include <iostream>
#include <string>

#include "client/client.hpp"
#include "log/log.h"
#include "server/server.hpp"

int main() {
  logd_level_set(&PROTOCOL_TAG, ERROR);
  logd_level_set(&SERVER_TAG, ERROR);
  logd_level_set(&CLIENT_TAG, ERROR);
  logd_level_set(&CONNECTION_TAG, ERROR);
  logd_level_set(&EPOLL_TAG, ERROR);
  log_level_set_all_OFF();

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
