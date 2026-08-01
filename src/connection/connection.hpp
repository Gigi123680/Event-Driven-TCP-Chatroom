/**
 * A connection object represents a TCP connection.
 * This can be both client-server and server-client connections.
 */
#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

enum ConnectionState {
  CONNECTING,
  SENDING_CLIENT_HELLO,     // client only
  WAITING_FOR_SERVER_HELLO, // client only
  SENDING_SERVER_HELLO,     // server only
  WAITING_FOR_CLIENT_HELLO, // server only
  ACTIVE,
  CLOSED,
  CONNECTION_ERROR
};

typedef struct Connection {
  int fd; // non blocking socket fd
  ConnectionState state;
  std::vector<char> in_buffer;
  std::vector<char> out_buffer;
  size_t bytes_written;
  std::string name; // name of the user on the other side
} Connection;

int server_create_listening_socket(int port);
Connection *client_connect_to_server(sockaddr_in *serv_addr);
bool client_check_connect(Connection *conn);
bool connection_flush_out_buffer(Connection *conn);
bool connection_read_into_in_buffer(Connection *conn);
void connection_cleanup(Connection *conn);
