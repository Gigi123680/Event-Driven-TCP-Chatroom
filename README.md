# Event-Driven TCP Chatroom

## Quick Demo

This project targets Linux.

```sh
git clone <repo-url>
cd chatroom
make
./chat
```

Run one terminal as the server:

```text
1. Server
Enter your username: host
Enter the port number to listen on: 8080
```

Run one or more other terminals as clients:

```text
2. Client
Enter your name: alice
Enter server IP address: 192.168.1.50
Enter server port: 8080
```

For an easy local-network demo, use the server machine's LAN IPv4 address, such
as `192.168.x.x` or `10.x.x.x`. You can usually find it with:

```sh
ip addr
```

Once connected, type normal chat messages and press Enter. Use `/quit` to leave
as a client or to close the room as the server.

## Overview

This is a small single-threaded TCP chatroom written in C++17. It uses Berkeley
sockets, non-blocking I/O, and `epoll` to handle terminal input and network
events in one event loop.

The chatroom uses a centralized model:

```text
            Server
          /   |   \
     Client Client Client
```

Clients send chat messages to the server. The server displays them locally and
broadcasts them to the other connected clients.

## Current Features

- Server mode and client mode in the same executable
- Multiple clients connected to one server
- Non-blocking TCP sockets
- `epoll`-based event loops
- Application-level client/server handshake
- Message framing over TCP
- Partial read/write buffering
- Broadcast messages
- Client `/quit`
- Server `/quit` room shutdown
- Basic disconnect handling

## Protocol

TCP is a byte stream, so the program defines a small frame format:

```text
+----------------------+--------------------------+
| Message Type: 1 byte | Payload Length: 4 bytes |
+----------------------+--------------------------+
| Payload: N bytes                                |
+-------------------------------------------------+
```

The payload length is encoded in network byte order. The current protocol
supports message types for client hello, server hello, chat, broadcast, error,
and connection close.

Payloads are limited to 64 KiB.

## Architecture

The main pieces are:

- `src/client`: client setup and client event loop
- `src/server`: server setup, accepting clients, broadcasting, and shutdown
- `src/connection`: socket setup plus connection read/write buffering
- `src/network`: `epoll` helpers and protocol framing/parsing
- `src/frontend`: terminal message formatting
- `src/log`: small C logger used by the C++ modules

Each connection stores its socket file descriptor, connection state, input
buffer, output buffer, write offset, and peer name.

## Limitations

- Linux only
- IPv4 only for now
- No encryption or authentication
- No terminal UI library
- Internet access may require firewall rules, router port forwarding, VPN, or a
  public server

## Build

```sh
make
```

Object files are placed under `build/`, and the executable is written to:

```text
./chat
```

Clean generated files with:

```sh
make clean
```
