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
#include "../log/log.h"
#include "../network/protocol.hpp"
#include "connection.hpp"

#define TAG &CONNECTION_TAG

/**
 * Attemps to connect to a server given its address. Returns a pointer to a
 * Connection struct if successful or connection underway; or nullptr if failed
 */
Connection *client_connect_to_server(sockaddr_in *serv_addr) {
  // create new socket
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    logd(TAG, ERROR, "Failed to create socket: %s\n", strerror(errno));
    return nullptr;
  }

  // set socket to non-blocking
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    logd(TAG, ERROR, "Failed to get socket flags: %s\n", strerror(errno));
    close(fd);
    return nullptr;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    logd(TAG, ERROR, "Failed to set socket to non-blocking mode: %s\n",
         strerror(errno));
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
    logd(TAG, ERROR, "Connection failed: %s\n", strerror(errno));
    close(fd);
    return nullptr;
  }
}
