#pragma once

#include <string>
#include <vector>

#include "../connection/connection.hpp"

void server_init();

typedef struct Server {
  std::string name; // server's name
  int port;         // port number to listen on
  int listen_fd;    // non blocking socket fd for listening to new connections
  int epoll_fd;
  std::vector<Connection *> connections; // all connected clients
} Server;
