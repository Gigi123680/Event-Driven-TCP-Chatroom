#include <string>

#include "../connection/connection.hpp"

void client_init();

typedef struct Client {
  std::string name;
  Connection *conn;
} Client;