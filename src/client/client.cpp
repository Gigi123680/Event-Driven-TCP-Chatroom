#include <arpa/inet.h>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <iostream>
#include <limits>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../connection/connection.hpp"
#include "../frontend/formatPrint.hpp"
#include "../log/log.h"
#include "../network/epoll.hpp"
#include "../network/protocol.hpp"
#include "client.hpp"

#define TAG &CLIENT_TAG

static Client *client;
static bool client_quitting = false;

void client_event_loop();
void client_cleanup();
static bool event_is_check_connection_success(int fd, uint32_t event_flags);
static bool handle_connection_success_event();
static bool event_is_read_network_message(int fd, uint32_t event_flags);
static bool handle_read_network_message_event();
static bool handle_network_message(const Message &message);
static bool event_is_send_network_message(int fd, uint32_t event_flags);
static bool handle_send_network_message_event();
static bool event_is_read_stdin(int fd, uint32_t event_flags);
static bool handle_read_stdin_event();
static bool connection_is_underway_or_established();
static bool handle_quit_command();
static bool name_is_reserved(const std::string &name);

/**
 * Initialize a client instance, and set up the connection to the server.
 */
void client_init() {
  std::cout << "Initializing client..." << std::endl;
  std::string name;
  while (true) {
    std::cout << "Enter your name: ";
    if (!(std::cin >> name)) {
      logd(TAG, ERROR, "Error reading client name.\n");
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      continue;
    }

    if (!name_is_reserved(name)) {
      break;
    }

    std::cout << "[app] reserved name." << std::endl;
  }
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

  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  client_event_loop();
}

static constexpr int MAX_EPOLL_EVENTS = 16;

/**
 * The main event loop for the client.
 */
void client_event_loop() {
  bool running = true;
  epoll_event events[MAX_EPOLL_EVENTS];

  int epoll_fd = create_epoll(client->conn, STDIN_FILENO);
  if (epoll_fd == -1) {
    logd(TAG, ERROR, "Failed to create epoll instance. Exiting...\n");
    goto c_cleanup;
  }
  client->epoll_fd = epoll_fd;

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

      if (client_quitting && !event_is_send_network_message(fd, event_flags)) {
        continue;
      }

      if (event_is_check_connection_success(fd, event_flags)) {
        if (!handle_connection_success_event()) {
          running = false;
          break;
        }
      } else if (event_is_send_network_message(fd, event_flags)) {
        if (!handle_send_network_message_event()) {
          running = false;
          break;
        }
      } else if (event_is_read_network_message(fd, event_flags)) {
        if (!handle_read_network_message_event()) {
          running = false;
          break;
        }
      } else if (event_is_read_stdin(fd, event_flags)) {
        if (!handle_read_stdin_event()) {
          running = false;
          break;
        }
      }
    }
  }
c_cleanup:
  client_cleanup();
}

/**
 * =========== Network read event ==================
 */
static bool event_is_read_network_message(int fd, uint32_t event_flags) {
  if (client_quitting) {
    return false;
  }

  return fd == client->conn->fd &&
         (client->conn->state == WAITING_FOR_SERVER_HELLO ||
          client->conn->state == ACTIVE) &&
         (event_flags & EPOLLIN);
}

static bool handle_read_network_message_event() {
  if (!connection_read_into_in_buffer(client->conn)) {
    return false;
  }

  // handle all complete messages
  while (true) {
    std::optional<Message> message = protocol_parse_message(client->conn);
    if (client->conn->state == CONNECTION_ERROR) {
      return false;
    }

    if (!message.has_value()) {
      return true;
    }

    if (!handle_network_message(message.value())) {
      return false;
    }
  }
}

static bool handle_network_message(const Message &message) {
  // parse SERVER_HELLO and transition state
  if (client->conn->state == WAITING_FOR_SERVER_HELLO) {
    if (message.type != SERVER_HELLO) {
      logd(TAG, ERROR, "Expected SERVER_HELLO, received type %d.\n",
           static_cast<int>(message.type));
      client->conn->state = CONNECTION_ERROR;
      return false;
    }

    client->conn->name = message.payload;
    client->conn->state = ACTIVE;
    std::cout << "Connected to server.\nName: " << client->conn->name
              << std::endl;
    std::cout << "[client] connected to server. Start chatting." << std::endl;
    return true;
  }

  // parse BROADCAST and print to stdout
  if (client->conn->state == ACTIVE) {
    if (message.type == CLOSE_CON) {
      if (!message.payload.empty()) {
        logd(TAG, ERROR, "Expected CLOSE_CON with empty payload.\n");
        client->conn->state = CONNECTION_ERROR;
        return false;
      }

      std::cout << "[client] server closed the room." << std::endl;
      client->conn->state = CLOSED;
      return false;
    }

    if (message.type != BROADCAST) {
      logd(TAG, ERROR, "Expected BROADCAST, received type %d.\n",
           static_cast<int>(message.type));
      client->conn->state = CONNECTION_ERROR;
      return false;
    }

    std::cout << message.payload << std::endl;
    return true;
  }

  logd(TAG, ERROR, "Unexpected message in current connection state.\n");
  client->conn->state = CONNECTION_ERROR;
  return false;
}

/**
 * ============ Network write event ==================
 */

static bool event_is_send_network_message(int fd, uint32_t event_flags) {
  return fd == client->conn->fd &&
         (client_quitting || client->conn->state == SENDING_CLIENT_HELLO ||
          client->conn->state == ACTIVE) &&
         (client->conn->out_buffer.size() > 0) && (event_flags & EPOLLOUT);
}

/**
 * Flush the out_buffer as much as possible. If all bytes are flushed, change
 * epoll to listen for read events.
 * @note If connection state is SENDING_CLIENT_HELLO, change state to
 * WAITING_FOR_SERVER_HELLO on full flush.
 */
static bool handle_send_network_message_event() {
  if (!connection_flush_out_buffer(client->conn)) {
    return false;
  }

  // partial write, wait for next EPOLLOUT event
  if (!client->conn->out_buffer.empty()) {
    return true;
  }

  if (client_quitting) {
    shutdown(client->conn->fd, SHUT_WR);
    return false;
  }

  if (client->conn->state == SENDING_CLIENT_HELLO) {
    client->conn->state = WAITING_FOR_SERVER_HELLO;
  }

  if (!epoll_fd_mod(client->epoll_fd, client->conn->fd, EPOLLIN | EPOLLRDHUP)) {
    return false;
  }

  return true;
}

static bool event_is_check_connection_success(int fd, uint32_t event_flags) {
  return fd == client->conn->fd && client->conn->state == CONNECTING &&
         (event_flags & EPOLLIN);
}

/**
 * =========== Check connection success event ============
 */

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

/**
 * =========== Stdin read event ============
 */

static bool event_is_read_stdin(int fd, uint32_t event_flags) {
  if (client_quitting) {
    return false;
  }

  return fd == STDIN_FILENO && (event_flags & EPOLLIN);
}

/**
 * Reads and queues a chat message from stdin. It is also printed with format to
 * stdout.
 * @note Empty messages are ignored. Input ignored if connection state is not
 * ACTIVE.
 */
static bool handle_read_stdin_event() {
  std::string line;
  if (!std::getline(std::cin, line)) {
    return false;
  }

  if (line.empty()) {
    return true;
  }

  if (line == "/quit" && connection_is_underway_or_established()) {
    return handle_quit_command();
  }

  if (client->conn->state != ACTIVE) {
    return true;
  }

  if (!protocol_payload_length_is_legal(line.size())) {
    std::cout << "[app] message length too long." << std::endl;
    return true;
  }

  Message chat{};
  chat.type = CHAT;
  chat.payload = line;
  chat.payload_length = static_cast<uint32_t>(chat.payload.size());

  if (!protocol_queue_message(client->conn, chat)) {
    return false;
  }

  format_print_message(client->name, chat);

  return epoll_fd_mod(client->epoll_fd, client->conn->fd,
                      EPOLLIN | EPOLLOUT | EPOLLRDHUP);
}

static bool connection_is_underway_or_established() {
  return client->conn != nullptr && client->conn->state != CLOSED &&
         client->conn->state != CONNECTION_ERROR;
}

static bool handle_quit_command() {
  std::cout << "[client] exiting." << std::endl;

  Message close_message{};
  close_message.type = CLOSE_CON;
  close_message.payload_length = 0;

  if (!protocol_queue_message(client->conn, close_message)) {
    return false;
  }

  client_quitting = true;
  return epoll_fd_mod(client->epoll_fd, client->conn->fd,
                      EPOLLOUT | EPOLLRDHUP);
}

static bool name_is_reserved(const std::string &name) {
  std::string lowercase_name;
  lowercase_name.reserve(name.size());
  for (char ch : name) {
    lowercase_name.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch))));
  }

  return lowercase_name == "client" || lowercase_name == "server" ||
         lowercase_name == "app";
}

void client_cleanup() {
  if (client) {
    if (client->conn) {
      connection_cleanup(client->conn);
      client->conn = nullptr;
    }
    if (client->epoll_fd != -1) {
      close(client->epoll_fd);
    }
    delete client;
  }
}
