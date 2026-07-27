/**
 * @file epoll.cpp
 * @brief Simple abstraction over epoll.
 */

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <limits>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>

#include "../connection/connection.hpp"
#include "../log/log.h"
#include "epoll.hpp"

#define TAG &EPOLL_TAG

/**
 * Generic epoll update function for add, remove, and modify operations.
 * @param epoll_fd The epoll file descriptor.
 * @param op The operation to perform (EPOLL_CTL_ADD, EPOLL_CTL_MOD,
 * EPOLL_CTL_DEL).
 * @param fd The file descriptor to update.
 */
static int epoll_update(int epoll_fd, int op, int fd, uint32_t events) {
  epoll_event event{};
  event.events = events;
  event.data.fd = fd;
  return epoll_ctl(epoll_fd, op, fd, &event);
}

/**
 * Registers a new file descriptor with the epoll instance.
 */
bool epoll_register(int epoll_fd, int fd, uint32_t events) {
  return epoll_fd_add(epoll_fd, fd, events);
}

/**
 * Adds a new file descriptor to the epoll instance.
 */
bool epoll_fd_add(int epoll_fd, int fd, uint32_t events) {
  if (epoll_update(epoll_fd, EPOLL_CTL_ADD, fd, events) == -1) {
    logd(TAG, ERROR, "Failed to add fd %d to epoll: %s\n", fd, strerror(errno));
    return false;
  }

  return true;
}

/**
 * Modifies the events for an existing file descriptor in the epoll instance.
 */
bool epoll_fd_mod(int epoll_fd, int fd, uint32_t events) {
  if (epoll_update(epoll_fd, EPOLL_CTL_MOD, fd, events) == -1) {
    logd(TAG, ERROR, "Failed to modify events for fd %d: %s\n", fd,
         strerror(errno));
    return false;
  }

  return true;
}

/**
 * Removes a file descriptor from the epoll instance.
 */
bool epoll_fd_remove(int epoll_fd, int fd, uint32_t events) {
  if (epoll_update(epoll_fd, EPOLL_CTL_DEL, fd, events) == -1) {
    logd(TAG, ERROR, "Failed to remove fd %d from epoll: %s\n", fd,
         strerror(errno));
    return false;
  }

  return true;
}

static uint32_t initial_socket_events(const Connection *conn) {
  uint32_t events = EPOLLIN | EPOLLRDHUP;

  // client waiting to send client hello
  if (conn->state == CONNECTING || conn->state == SENDING_CLIENT_HELLO ||
      !conn->out_buffer.empty()) {
    events |= EPOLLOUT;
  }
  // server waiting for client hello
  else if (conn->state == CONNECTING ||
           conn->state == WAITING_FOR_CLIENT_HELLO) {
    events |= EPOLLIN;
  }

  return events;
}

/**
 * Creates an epoll instance and adds the socket and stdin to it.
 * Socket events are determined based on the connection state.
 * Stdin is always monitored for input.
 * @param conn The connection object containing the socket file descriptor.
 * @param STDIN_FD The file descriptor for standard input (stdin).
 * @return The epoll file descriptor on success, or -1 on failure.
 */
int create_epoll(const Connection *conn, int STDIN_FD) {
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    logd(TAG, ERROR, "Failed to create epoll instance: %s\n", strerror(errno));
    return -1;
  }

  // add socket to epoll
  if (!epoll_register(epoll_fd, conn->fd, initial_socket_events(conn))) {
    close(epoll_fd);
    return -1;
  }

  // add stdin to epoll
  if (!epoll_register(epoll_fd, STDIN_FD, EPOLLIN)) {
    close(epoll_fd);
    return -1;
  }

  return epoll_fd;
}
