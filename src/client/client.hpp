#include <string>

#include "../connection/connection.hpp"

void client_init();

typedef struct Client {
  std::string name; // client's name
  Connection *conn;
  int epoll_fd;
} Client;