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
 * Attempt to establish a TCP connection to the server.
 * @return A pointer to a Connection object if the connection is successful or
 * in progress, nullptr otherwise.
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

/**
 * Check if the TCP connection has been successfully established.
 * Should be called when connections state is CONNECTING.
 */
bool client_check_connect(Connection *conn) {
  int socket_error = 0;
  socklen_t socket_error_len = sizeof(socket_error);

  if (getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &socket_error,
                 &socket_error_len) == -1) {
    logd(TAG, ERROR, "Failed to check connection status: %s\n",
         strerror(errno));
    conn->state = CONNECTION_ERROR;
    return false;
  }

  if (socket_error != 0) {
    logd(TAG, ERROR, "Connection failed: %s\n", strerror(socket_error));
    conn->state = CONNECTION_ERROR;
    return false;
  }

  conn->state = SENDING_CLIENT_HELLO;
  logd(TAG, INFO, "TCP connection to server established.\n");
  return true;
}
