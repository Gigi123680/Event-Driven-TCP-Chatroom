#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "../client/client.hpp"
#include "../protocol/protocol.hpp"
#include "connection.hpp"

/**
 * Attemps to connect to a server given its address. Returns a pointer to a
 * Connection struct if successful or connection underway; or nullptr if failed
 */
Connection *client_connect_to_server(sockaddr_in *serv_addr) {
  // create new socket
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    std::cerr << "[connection] Failed to create socket.\n";
    return nullptr;
  }

  // set socket to non-blocking
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    std::cerr << "[connection] Failed to get socket flags.\n";
    close(fd);
    return nullptr;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    std::cerr << "[connection] Failed to set socket to non-blocking mode.\n";
    close(fd);
    return nullptr;
  }

  // attempt connection
  int result =
      connect(fd, reinterpret_cast<sockaddr *>(serv_addr), sizeof(*serv_addr));
  if (result == 0) {
    Connection *conn = new Connection();
    conn->fd = fd;
    conn->state = SENDING_CLIENT_HELLO;
    return conn;
  } else if (result == -1 && errno == EINPROGRESS) {
    Connection *conn = new Connection();
    conn->fd = fd;
    conn->state = CONNECTING;
    return conn;
  } else {
    std::cerr << "[connection] connection failed.\n";
    close(fd);
    return nullptr;
  }
}
