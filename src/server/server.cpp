#include <cctype>
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
static bool server_closing = false;

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
static bool announce_client_disconnected(Connection *conn);
static bool announce_room_closing();
static bool broadcast_message_to_other_clients(Connection *sender,
                                               const Message &message);
static bool queue_close_to_all_clients();
static bool all_connection_buffers_empty();
static bool remove_connection(Connection *conn);
static Connection *get_connection_of(int fd);
static bool name_is_reserved(const std::string &name);

void server_init() {
  std::cout << "Server mode selected." << std::endl;
  std::string username;
  while (true) {
    std::cout << "Enter your username: ";
    if (!(std::cin >> username)) {
      logd(TAG, ERROR, "Error reading server username.\n");
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      continue;
    }

    if (!name_is_reserved(username)) {
      break;
    }

    std::cout << "[app] reserved name." << std::endl;
  }
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
  logd(TAG, INFO, "Epoll instance created.\n");

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

      if (server_closing && !event_is_send_network_package(fd, event_flags)) {
        continue;
      }

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

    if (server_closing && all_connection_buffers_empty()) {
      running = false;
    }
  }
s_cleanup:
  server_cleanup();
}

/**
 * ========== Accept new client connection event ============
 */

static bool event_is_accept_client_connection(int fd, uint32_t event_flags) {
  if (server_closing) {
    return false;
  }

  return fd == server->listen_fd && (event_flags & EPOLLIN);
}

static bool handle_accept_client_connection_event() {
  // accept all new client connection
  while (true) {
    logd(TAG, INFO, "Accepting new client connection...\n");
    sockaddr_in client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd =
        accept4(server->listen_fd, reinterpret_cast<sockaddr *>(&client_addr),
                &client_addr_len, SOCK_NONBLOCK);

    if (client_fd == -1) {
      if (errno == EINTR) {
        logd(TAG, INFO, "accept4 interrupted by signal, returning...\n");
        return true;
      }

      // No more clients
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        logd(TAG, INFO, "No more clients to accept, returning...\n");
        return true;
      }

      logd(TAG, ERROR, "Failed to accept client connection: %s\n",
           strerror(errno));
      return false;
    }
    logd(TAG, INFO, "Accepted new client connection, fd: %d\n", client_fd);

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
    // print current connections
    for (Connection *c : server->connections) {
      logd(TAG, INFO, "Current connections:\n");
      logd(TAG, INFO, "  fd: %d, state: %d\n", c->fd,
           static_cast<int>(c->state));
    }
  }
}

/**
 * ========== Receive CLIENT_HELLO event ============
 */

static bool event_is_receive_client_hello(int fd, uint32_t event_flags) {
  if (server_closing) {
    return false;
  }

  Connection *conn = get_connection_of(fd);
  return conn != nullptr && conn->state == WAITING_FOR_CLIENT_HELLO &&
         (event_flags & (EPOLLIN | EPOLLRDHUP | EPOLLHUP));
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
    if (conn->state == CLOSED) {
      return remove_connection(conn);
    }

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
  if (server_closing) {
    return false;
  }

  Connection *conn = get_connection_of(fd);
  return conn != nullptr && conn->state == ACTIVE &&
         (event_flags & (EPOLLIN | EPOLLRDHUP | EPOLLHUP));
}

static bool handle_read_network_package_event(int fd) {
  logd(TAG, INFO, "Handling network read event.\n");
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
      if (conn->state == CLOSED) {
        if (!announce_client_disconnected(conn)) {
          return false;
        }

        return remove_connection(conn);
      }

      return true;
    }

    if (message->type == CLOSE_CON) {
      logd(TAG, INFO, "Received CLOSE_CON from %s.\n", conn->name.c_str());
      if (!message->payload.empty()) {
        logd(TAG, ERROR, "Expected CLOSE_CON with empty payload.\n");
        conn->state = CONNECTION_ERROR;
        return false;
      }

      if (!announce_client_disconnected(conn)) {
        return false;
      }

      return remove_connection(conn);
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
  if (server_closing) {
    return false;
  }

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

  if (line == "/quit") {
    server_closing = true;
    if (!announce_room_closing()) {
      return false;
    }
    if (!queue_close_to_all_clients()) {
      return false;
    }
    return !all_connection_buffers_empty();
  }

  if (!protocol_payload_length_is_legal(line.size())) {
    std::cout << "[app] message length too long." << std::endl;
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

  format_print_message("server", notice);

  Message broadcast = format_broadcast_message("server", notice);
  return broadcast_message_to_other_clients(nullptr, broadcast);
}

static bool announce_client_disconnected(Connection *conn) {
  logd(TAG, INFO, "Announcing disconnection of %s.\n", conn->name.c_str());
  Message notice{};
  notice.type = CHAT;
  notice.payload = "`" + conn->name + "` disconnected.";
  notice.payload_length = static_cast<uint32_t>(notice.payload.size());

  format_print_message("server", notice);

  logd(TAG, INFO, "Broadcasting disconnection of %s to other clients.\n",
       conn->name.c_str());
  Message broadcast = format_broadcast_message("server", notice);
  return broadcast_message_to_other_clients(conn, broadcast);
}

static bool announce_room_closing() {
  Message notice{};
  notice.type = CHAT;
  notice.payload = "room is closing.";
  notice.payload_length = static_cast<uint32_t>(notice.payload.size());

  format_print_message("server", notice);

  Message broadcast = format_broadcast_message("server", notice);
  return broadcast_message_to_other_clients(nullptr, broadcast);
}

static bool queue_close_to_all_clients() {
  Message close_message{};
  close_message.type = CLOSE_CON;
  close_message.payload_length = 0;

  for (Connection *conn : server->connections) {
    if (conn->state != ACTIVE) {
      continue;
    }

    if (!protocol_queue_message(conn, close_message)) {
      return false;
    }

    if (!epoll_fd_mod(server->epoll_fd, conn->fd, EPOLLOUT | EPOLLRDHUP)) {
      return false;
    }
  }

  return true;
}

static bool all_connection_buffers_empty() {
  for (Connection *conn : server->connections) {
    if (!conn->out_buffer.empty()) {
      return false;
    }
  }

  return true;
}

static bool remove_connection(Connection *conn) {
  if (conn == nullptr) {
    return true;
  }

  epoll_fd_remove(server->epoll_fd, conn->fd, 0);

  for (auto it = server->connections.begin(); it != server->connections.end();
       ++it) {
    if (*it == conn) {
      server->connections.erase(it);
      connection_cleanup(conn);
      return true;
    }
  }

  connection_cleanup(conn);
  return true;
}

/**
 * Cached connection lookup for fd -> connection obj.
 * @returns nullptr if no connection found.
 */
static Connection *get_connection_of(int fd) {
  static Connection *cached_connection = nullptr;

  if (cached_connection != nullptr) {
    bool cache_still_valid = false;
    for (Connection *conn : server->connections) {
      if (conn == cached_connection) {
        cache_still_valid = true;
        break;
      }
    }

    if (cache_still_valid && cached_connection->fd == fd) {
      return cached_connection;
    }

    cached_connection = nullptr;
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

static bool name_is_reserved(const std::string &name) {
  std::string lowercase_name;
  lowercase_name.reserve(name.size());
  for (char ch : name) {
    lowercase_name.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }

  return lowercase_name == "client" || lowercase_name == "server" ||
         lowercase_name == "app";
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
