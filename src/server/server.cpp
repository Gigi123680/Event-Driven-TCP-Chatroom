#include <cerrno>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../frontend/formatPrint.hpp"
#include "../log/log.h"
#include "../network/epoll.hpp"
#include "../network/protocol.hpp"
#include "server.hpp"

#define TAG &SERVER_TAG

static Server *server;

void server_event_loop();
void server_cleanup();
static bool event_is_accept_client_connection(int fd, uint32_t event_flags);
static bool handle_accept_client_connection_event();
static bool event_is_receive_client_hello(int fd, uint32_t event_flags);
static bool handle_receive_client_hello_event(int fd);
static bool event_is_send_network_package(int fd, uint32_t event_flags);
static bool handle_send_network_package_event(int fd);
static bool event_is_read_network_package(int fd, uint32_t event_flags);
static bool handle_read_network_package_event(int fd);
static bool event_is_read_stdin(int fd, uint32_t event_flags);
static bool handle_read_stdin_event();
static bool announce_client_connected(Connection *conn);
static bool broadcast_message_to_other_clients(Connection *sender,
                                               const Message &message);
static Connection *get_connection_of(int fd);

void server_init() {
  std::cout << "Server mode selected." << std::endl;
  std::cout << "Enter your username: ";
  std::string username;
  std::cin >> username;
  std::cout << "Your username is: " << username << std::endl;

  int port;
  while (true) {
    std::cout << "Enter the port number to listen on: ";
    if (std::cin >> port) {
      break;
    }

    std::cout << "Invalid input. Please enter an integer." << std::endl;
    std::cin.clear(); // Clear the error flag
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  std::cout << "Port number is: " << port << std::endl;

  // initialize server with listening socket
  std::cout << "Initializing server instance..." << std::endl;
  server = new Server();
  server->name = username;
  server->port = port;
  server->listen_fd = server_create_listening_socket(server->port);
  server->epoll_fd = -1;
  if (server->listen_fd == -1) {
    logd(TAG, ERROR, "Failed to create listening socket.\n");
    return;
  }
  logd(TAG, INFO, "Server listening on port %d\n", server->port);

  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  server_event_loop();
}

static constexpr int MAX_EPOLL_EVENTS = 16;

void server_event_loop() {
  epoll_event events[MAX_EPOLL_EVENTS];
  bool running = true;

  // create epoll
  server->epoll_fd = create_epoll(server->listen_fd, STDIN_FILENO);
  if (server->epoll_fd == -1) {
    logd(TAG, ERROR, "Failed to create epoll instance.\n");
    close(server->listen_fd);
    server->listen_fd = -1;
    goto s_cleanup;
  }

  while (running) {
    int event_count =
        epoll_wait(server->epoll_fd, events, MAX_EPOLL_EVENTS, -1);
    if (event_count == -1) {
      if (errno == EINTR) {
        continue;
      }

      logd(TAG, ERROR, "epoll_wait failed: %s\n", strerror(errno));
      break;
    }

    for (int i = 0; i < event_count; ++i) {
      int fd = events[i].data.fd;
      uint32_t event_flags = events[i].events;

      if (event_is_accept_client_connection(fd, event_flags)) {
        if (!handle_accept_client_connection_event()) {
          running = false;
          break;
        }
      } else if (event_is_receive_client_hello(fd, event_flags)) {
        if (!handle_receive_client_hello_event(fd)) {
          running = false;
          break;
        }
      } else if (event_is_send_network_package(fd, event_flags)) {
        if (!handle_send_network_package_event(fd)) {
          running = false;
          break;
        }
      } else if (event_is_read_network_package(fd, event_flags)) {
        if (!handle_read_network_package_event(fd)) {
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
s_cleanup:
  server_cleanup();
}

/**
 * ========== Accept new client connection event ============
 */

static bool event_is_accept_client_connection(int fd, uint32_t event_flags) {
  return fd == server->listen_fd && (event_flags & EPOLLIN);
}

static bool handle_accept_client_connection_event() {
  while (true) {
    // accept new client connection
    sockaddr_in client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd =
        accept4(server->listen_fd, reinterpret_cast<sockaddr *>(&client_addr),
                &client_addr_len, SOCK_NONBLOCK);

    if (client_fd == -1) {
      if (errno == EINTR) {
        return true;
      }

      // No more clients
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return true;
      }

      logd(TAG, ERROR, "Failed to accept client connection: %s\n",
           strerror(errno));
      return false;
    }

    // create client connection object
    Connection *conn = new Connection();
    conn->fd = client_fd;
    conn->state = WAITING_FOR_CLIENT_HELLO;
    conn->bytes_written = 0;
    server->connections.push_back(conn);

    // register into epoll
    if (!epoll_register(server->epoll_fd, conn->fd, EPOLLIN | EPOLLRDHUP)) {
      server->connections.pop_back();
      connection_cleanup(conn);
      return false;
    }

    logd(TAG, INFO, "Accepted new client connection.\n");
  }
}

/**
 * ========== Receive CLIENT_HELLO event ============
 */

static bool event_is_receive_client_hello(int fd, uint32_t event_flags) {
  Connection *conn = get_connection_of(fd);
  return conn != nullptr && conn->state == WAITING_FOR_CLIENT_HELLO &&
         (event_flags & EPOLLIN);
}

static bool handle_receive_client_hello_event(int fd) {
  Connection *conn = get_connection_of(fd); // client connection obj
  if (conn == nullptr) {
    logd(TAG, ERROR, "No connection found for fd %d.\n", fd);
    return false;
  }

  // read from socket
  if (!connection_read_into_in_buffer(conn)) {
    return false;
  }

  // parse network messages
  std::optional<Message> message = protocol_parse_message(conn);
  if (conn->state == CONNECTION_ERROR) {
    return false;
  }

  // no complete message yet
  if (!message.has_value()) {
    return true;
  }

  // verify CLIENT_HELLO
  if (message->type != CLIENT_HELLO) {
    logd(TAG, ERROR, "Expected CLIENT_HELLO, received type %d.\n",
         static_cast<int>(message->type));
    conn->state = CONNECTION_ERROR;
    return false;
  }

  if (message->payload.empty()) {
    logd(TAG, ERROR, "Client sent empty name.\n");
    conn->state = CONNECTION_ERROR;
    return false;
  }

  conn->name = message->payload;

  // prepare and queue SERVER_HELLO
  Message hello{};
  hello.type = SERVER_HELLO;
  hello.payload = server->name;
  hello.payload_length = static_cast<uint32_t>(hello.payload.size());
  if (!protocol_queue_message(conn, hello)) {
    return false;
  }

  // update epoll events for write and read
  conn->state = SENDING_SERVER_HELLO;
  if (!epoll_fd_mod(server->epoll_fd, conn->fd,
                    EPOLLIN | EPOLLOUT | EPOLLRDHUP)) {
    return false;
  }

  logd(TAG, INFO, "Received CLIENT_HELLO from %s.\n", conn->name.c_str());
  return true;
}

/**
 * ========== Network write event ============
 */

static bool event_is_send_network_package(int fd, uint32_t event_flags) {
  Connection *conn = get_connection_of(fd);
  return conn != nullptr &&
         (conn->state == SENDING_SERVER_HELLO || conn->state == ACTIVE) &&
         !conn->out_buffer.empty() && (event_flags & EPOLLOUT);
}

static bool handle_send_network_package_event(int fd) {
  Connection *conn = get_connection_of(fd);
  if (conn == nullptr) {
    logd(TAG, ERROR, "No connection found for fd %d.\n", fd);
    return false;
  }

  if (!connection_flush_out_buffer(conn)) {
    return false;
  }

  // Partial write, wait for next EPOLLOUT event.
  if (!conn->out_buffer.empty()) {
    return true;
  }

  if (conn->state == SENDING_SERVER_HELLO) {
    conn->state = ACTIVE;
    return announce_client_connected(conn);
  }

  return epoll_fd_mod(server->epoll_fd, conn->fd, EPOLLIN | EPOLLRDHUP);
}

/**
 * ========== Network read event ============
 */

static bool event_is_read_network_package(int fd, uint32_t event_flags) {
  Connection *conn = get_connection_of(fd);
  return conn != nullptr && conn->state == ACTIVE && (event_flags & EPOLLIN);
}

static bool handle_read_network_package_event(int fd) {
  Connection *conn = get_connection_of(fd);
  if (conn == nullptr) {
    logd(TAG, ERROR, "No connection found for fd %d.\n", fd);
    return false;
  }

  if (!connection_read_into_in_buffer(conn)) {
    return false;
  }

  // read all available messages and broadcast to connected clients
  while (true) {
    std::optional<Message> message = protocol_parse_message(conn);
    if (conn->state == CONNECTION_ERROR) {
      return false;
    }

    if (!message.has_value()) {
      return true;
    }

    if (message->type != CHAT) {
      logd(TAG, ERROR, "Expected CHAT, received type %d.\n",
           static_cast<int>(message->type));
      conn->state = CONNECTION_ERROR;
      return false;
    }

    // print to STDOUT
    format_print_message(conn->name, message.value());

    // broadcast to other clients
    Message broadcast = format_broadcast_message(conn->name, message.value());
    if (!broadcast_message_to_other_clients(conn, broadcast)) {
      return false;
    }
  }
}

/**
 * Queues a broadcast message to all connected and active clients except the
 * sender. Updates epoll events for each client to listen for write events.
 */
static bool broadcast_message_to_other_clients(Connection *sender,
                                               const Message &message) {
  for (Connection *conn : server->connections) {
    if (conn == sender || conn->state != ACTIVE) {
      continue;
    }

    if (!protocol_queue_message(conn, message)) {
      return false;
    }

    if (!epoll_fd_mod(server->epoll_fd, conn->fd,
                      EPOLLIN | EPOLLOUT | EPOLLRDHUP)) {
      return false;
    }
  }

  return true;
}

/**
 * ========== Stdin read event ============
 */

static bool event_is_read_stdin(int fd, uint32_t event_flags) {
  return fd == STDIN_FILENO && (event_flags & EPOLLIN);
}

static bool handle_read_stdin_event() {
  std::string line;
  if (!std::getline(std::cin, line)) {
    return false;
  }

  if (line.empty()) {
    return true;
  }

  Message chat{};
  chat.type = CHAT;
  chat.payload = line;
  chat.payload_length = static_cast<uint32_t>(chat.payload.size());

  format_print_message(server->name, chat);

  Message broadcast = format_broadcast_message(server->name, chat);
  return broadcast_message_to_other_clients(nullptr, broadcast);
}

/**
 * ========== Helpers ============
 */

static bool announce_client_connected(Connection *conn) {
  Message notice{};
  notice.type = CHAT;
  notice.payload = "`" + conn->name + "` connected.";
  notice.payload_length = static_cast<uint32_t>(notice.payload.size());

  format_print_message(server->name, notice);

  Message broadcast = format_broadcast_message(server->name, notice);
  return broadcast_message_to_other_clients(nullptr, broadcast);
}

/**
 * Cached connection lookup for fd -> connection obj.
 * @returns nullptr if no connection found.
 */
static Connection *get_connection_of(int fd) {
  static Connection *cached_connection = nullptr;

  if (cached_connection != nullptr && cached_connection->fd == fd) {
    return cached_connection;
  }

  for (Connection *conn : server->connections) {
    if (conn->fd == fd) {
      cached_connection = conn;
      return conn;
    }
  }

  cached_connection = nullptr;
  return nullptr;
}

void server_cleanup() {
  if (server) {
    if (server->listen_fd != -1) {
      close(server->listen_fd);
      server->listen_fd = -1;
    }

    if (server->epoll_fd != -1) {
      close(server->epoll_fd);
      server->epoll_fd = -1;
    }

    for (Connection *conn : server->connections) {
      connection_cleanup(conn);
    }
    server->connections.clear();

    delete server;
    server = nullptr;
  }
}
