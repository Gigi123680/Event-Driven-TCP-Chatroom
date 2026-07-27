#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <limits>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../connection/connection.hpp"
#include "../log/log.h"
#include "../network/epoll.hpp"
#include "../network/protocol.hpp"
#include "client.hpp"

#define TAG &CLIENT_TAG

static Client *client;

void client_event_loop();
void client_cleanup();
static bool event_is_check_connection_success(int fd, uint32_t event_flags);
static bool handle_connection_success_event();

/**
 * Initialize a client instance, and set up the connection to the server.
 */
void client_init() {
  std::cout << "Initializing client..." << std::endl;
  std::cout << "Enter your name: ";
  std::string name;
  std::cin >> name;
  std::cout << "Your name is: " << name << std::endl;
  client = new Client();
  client->name = name;

  std::string server_ip;
  sockaddr_in server_addr{};
  while (true) {
    std::cout << "Enter server IP address: ";
    if (!(std::cin >> server_ip)) {
      logd(TAG, ERROR, "Error reading server IP address.\n");
      std::cin.clear(); // Clear the error flag
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      continue;
    }

    // convert to binary form
    server_addr = {};
    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) == 1) {
      break;
    }

    logd(TAG, ERROR, "Invalid IPv4 address.\n");
  }

  int server_port;
  while (true) {
    std::cout << "Enter server port: ";
    if (!(std::cin >> server_port)) {
      logd(TAG, ERROR, "Error reading server port.\n");
      std::cin.clear(); // Clear the error flag
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      continue;
    }

    if (server_port >= 1 && server_port <= 65535) {
      break;
    }

    logd(TAG, ERROR, "Port must be between 1 and 65535.\n");
  }
  server_addr.sin_port = htons(static_cast<uint16_t>(server_port));

  Connection *connection = client_connect_to_server(&server_addr);
  if (connection == nullptr) {
    logd(TAG, ERROR, "Failed to start connection to server. Exiting...\n");
    return;
  }
  client->conn = connection;
  std::cout << "Connection setup started." << std::endl;

  client_event_loop();
}

static constexpr int MAX_EPOLL_EVENTS = 16;

/**
 * The main event loop for the client.
 */
void client_event_loop() {
  int epoll_fd = create_epoll(client->conn, STDIN_FILENO);
  if (epoll_fd == -1) {
    return;
  }
  client->epoll_fd = epoll_fd;

  epoll_event events[MAX_EPOLL_EVENTS];
  bool running = true;

  while (running) {
    // retreive active events
    int event_count = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
    if (event_count == -1) {
      if (errno == EINTR) {
        continue;
      }

      logd(TAG, ERROR, "epoll_wait failed: %s\n", strerror(errno));
      break;
    }

    // handle events
    for (int i = 0; i < event_count; ++i) {
      int fd = events[i].data.fd;
      uint32_t event_flags = events[i].events;

      if (event_is_check_connection_success(fd, event_flags)) {
        if (!handle_connection_success_event()) {
          running = false;
          break;
        }
      }
    }
  }

  client_cleanup();
}

static bool event_is_check_connection_success(int fd, uint32_t event_flags) {
  return fd == client->conn->fd && client->conn->state == CONNECTING &&
         (event_flags & EPOLLIN);
}

/**
 * Construct and queue the CLIENT_HELLO message, and modify epoll to listen for
 * write events.
 * @return true if message is succesfully queued and epoll modified
 */
static bool handle_connection_success_event() {
  if (!client_check_connect(client->conn)) {
    return false;
  }

  // construct and queue CLIENT_HELLO message
  Message hello{};
  hello.type = CLIENT_HELLO;
  hello.payload = client->name;
  hello.payload_length = static_cast<uint32_t>(hello.payload.size());
  if (!protocol_queue_message(client->conn, hello)) {
    return false;
  }

  // modify epoll events to listen for write event.
  if (!epoll_fd_mod(client->epoll_fd, client->conn->fd,
                    EPOLLOUT | EPOLLRDHUP)) {
    return false;
  }

  return true;
}

void client_cleanup() {
  if (client) {
    if (client->conn) {
      close(client->conn->fd);
      delete client->conn;
    }
    if (client->epoll_fd != -1) {
      close(client->epoll_fd);
    }
    delete client;
  }
}
