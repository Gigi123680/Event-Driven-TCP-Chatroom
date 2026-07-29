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
 * Set a socket to non-blocking mode.
 */
static bool set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    logd(TAG, ERROR, "Failed to get socket flags: %s\n", strerror(errno));
    return false;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    logd(TAG, ERROR, "Failed to set socket to non-blocking mode: %s\n",
         strerror(errno));
    return false;
  }

  return true;
}

int server_create_listening_socket(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd == -1) {
    logd(TAG, ERROR, "Failed to create listening socket: %s\n",
         strerror(errno));
    return -1;
  }

  int reuse = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
    logd(TAG, ERROR, "Failed to set SO_REUSEADDR: %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  if (!set_nonblocking(fd)) {
    close(fd);
    return -1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(static_cast<uint16_t>(port));

  // bind socket to port
  if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
    logd(TAG, ERROR, "Failed to bind listening socket: %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  // set socket to listen
  if (listen(fd, SOMAXCONN) == -1) {
    logd(TAG, ERROR, "Failed to listen on socket: %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  return fd;
}

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
  if (!set_nonblocking(fd)) {
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
    conn->bytes_written = 0;
    return conn;
  } else if (result == -1 && errno == EINPROGRESS) {
    Connection *conn = new Connection();
    conn->fd = fd;
    conn->state = CONNECTING;
    conn->bytes_written = 0;
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

/**
 * Try to flush as many queued outgoing bytes as the socket will currently
 * accept. Partial writes are normal for non-blocking sockets.
 */
bool connection_flush_out_buffer(Connection *conn) {
  while (conn->bytes_written < conn->out_buffer.size()) {
    const char *data = conn->out_buffer.data() + conn->bytes_written;
    size_t remaining = conn->out_buffer.size() - conn->bytes_written;

    ssize_t sent = send(conn->fd, data, remaining, MSG_NOSIGNAL);
    // written
    if (sent > 0) {
      conn->bytes_written += static_cast<size_t>(sent);
      continue;
    }

    // signal interrupt, try later
    if (sent == -1 && errno == EINTR) {
      return true;
    }

    // socket buffer full, try later
    if (sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return true;
    }

    // all other errors are fatal
    logd(TAG, ERROR, "Failed to write to socket: %s\n", strerror(errno));
    conn->state = CONNECTION_ERROR;
    return false;
  }

  // buffer fully written, clear and reset
  conn->out_buffer.clear();
  conn->bytes_written = 0;
  return true;
}

/**
 * Read all immediately available socket bytes into the connection's in_buffer.
 * Message parsing is not implemented, refer to protocol_parse_message for that.
 */
bool connection_read_into_in_buffer(Connection *conn) {
  char buffer[4096];

  while (true) {
    ssize_t received = recv(conn->fd, buffer, sizeof(buffer), 0);

    // append to in_buffer
    if (received > 0) {
      conn->in_buffer.insert(conn->in_buffer.end(), buffer, buffer + received);
      continue;
    }

    // handle errors
    if (received == 0) {
      logd(TAG, INFO, "Peer closed the connection.\n");
      conn->state = CLOSED;
      return false;
    }
    if (errno == EINTR) {
      return true;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return true;
    }

    logd(TAG, ERROR, "Failed to read from socket: %s\n", strerror(errno));
    conn->state = CONNECTION_ERROR;
    return false;
  }
}

void connection_cleanup(Connection *conn) {
  if (conn == nullptr) {
    return;
  }

  if (conn->fd != -1) {
    close(conn->fd);
    conn->fd = -1;
  }

  delete conn;
}
