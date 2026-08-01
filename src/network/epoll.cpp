/**
 * @file epoll.cpp
 * @brief Simple abstraction over epoll.
 */

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <limits>
#ifndef __linux__
#include <map>
#include <poll.h>
#include <vector>
#endif
#include <netinet/in.h>
#include <string>
#include <unistd.h>

#include "../connection/connection.hpp"
#include "../log/log.h"
#include "epoll.hpp"

#define TAG &EPOLL_TAG

#ifndef __linux__
struct PollEntry {
  int fd;
  uint32_t events;
};

// epoll_fd to vector of watched fd map
static std::map<int, std::vector<PollEntry>> poll_instances;
static int next_poll_instance = -2;

static short epoll_events_to_poll(uint32_t events) {
  short poll_events = 0;
  if (events & EPOLLIN) {
    poll_events |= POLLIN;
  }
  if (events & EPOLLOUT) {
    poll_events |= POLLOUT;
  }
  return poll_events;
}

static uint32_t poll_events_to_epoll(short events) {
  uint32_t epoll_events = 0;
  if (events & (POLLIN | POLLPRI)) {
    epoll_events |= EPOLLIN;
  }
  if (events & POLLOUT) {
    epoll_events |= EPOLLOUT;
  }
  if (events & POLLERR) {
    epoll_events |= EPOLLERR;
  }
  if (events & POLLHUP) {
    epoll_events |= EPOLLHUP | EPOLLRDHUP;
  }
  return epoll_events;
}

int epoll_create1(int flags) {
  (void)flags;
  int epoll_fd = next_poll_instance--;
  poll_instances[epoll_fd] = {};
  return epoll_fd;
}

int epoll_ctl(int epoll_fd, int op, int fd, epoll_event *event) {
  auto instance = poll_instances.find(epoll_fd);
  if (instance == poll_instances.end()) {
    errno = EBADF;
    return -1;
  }

  std::vector<PollEntry> &entries = instance->second;
  auto entry = entries.begin();
  for (; entry != entries.end(); ++entry) {
    if (entry->fd == fd) {
      break;
    }
  }

  if (op == EPOLL_CTL_ADD) {
    if (entry != entries.end()) {
      errno = EEXIST;
      return -1;
    }
    entries.push_back({fd, event ? event->events : 0});
    return 0;
  }

  if (op == EPOLL_CTL_MOD) {
    if (entry == entries.end()) {
      errno = ENOENT;
      return -1;
    }
    entry->events = event ? event->events : 0;
    return 0;
  }

  if (op == EPOLL_CTL_DEL) {
    if (entry == entries.end()) {
      errno = ENOENT;
      return -1;
    }
    entries.erase(entry);
    return 0;
  }

  errno = EINVAL;
  return -1;
}

int epoll_wait(int epoll_fd, epoll_event *events, int maxevents, int timeout) {
  auto instance = poll_instances.find(epoll_fd);
  if (instance == poll_instances.end()) {
    errno = EBADF;
    return -1;
  }

  std::vector<pollfd> poll_fds;
  poll_fds.reserve(instance->second.size());
  for (const PollEntry &entry : instance->second) {
    poll_fds.push_back({entry.fd, epoll_events_to_poll(entry.events), 0});
  }

  int ready_count = poll(poll_fds.data(), poll_fds.size(), timeout);
  if (ready_count <= 0) {
    return ready_count;
  }

  int event_count = 0;
  for (size_t i = 0; i < poll_fds.size() && event_count < maxevents; ++i) {
    uint32_t epoll_events = poll_events_to_epoll(poll_fds[i].revents);
    if (epoll_events == 0) {
      continue;
    }

    events[event_count].events = epoll_events;
    events[event_count].data.fd = poll_fds[i].fd;
    ++event_count;
  }

  return event_count;
}
#endif

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

static uint32_t initial_socket_events(int socket_fd) {
  (void)socket_fd;
  return EPOLLIN;
}

static int create_epoll_with_socket_events(int socket_fd, int STDIN_FD,
                                           uint32_t socket_events) {
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    logd(TAG, ERROR, "Failed to create epoll instance: %s\n", strerror(errno));
    return -1;
  }

  // add socket to epoll
  if (!epoll_register(epoll_fd, socket_fd, socket_events)) {
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

/**
 * Creates an epoll instance and adds the socket and stdin to it.
 * Socket events are determined based on the connection state.
 * Stdin is always monitored for input.
 * @param conn The connection object containing the socket file descriptor.
 * @param STDIN_FD The file descriptor for standard input (stdin).
 * @return The epoll file descriptor on success, or -1 on failure.
 */
int create_epoll(const Connection *conn, int STDIN_FD) {
  return create_epoll_with_socket_events(conn->fd, STDIN_FD,
                                         initial_socket_events(conn));
}

/**
 * Creates an epoll instance and adds the socket and stdin to it.
 * Socket events are determined based on the socket state.
 * Stdin is always monitored for input.
 */
int create_epoll(int socket_fd, int STDIN_FD) {
  return create_epoll_with_socket_events(socket_fd, STDIN_FD,
                                         initial_socket_events(socket_fd));
}
