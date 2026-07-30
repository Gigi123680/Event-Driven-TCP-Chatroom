Programming Assignment: Event-Driven TCP Chatroom
Overview
Build a small Linux chat application in C++ using Berkeley sockets and `epoll`.
The program should support two modes:
Host mode  
The program acts as the central server for a chatroom.
Client mode  
The program connects to a host using the host's IP address and port.
Although every copy of the program may be able to run as either a host or a client, each individual chatroom uses a centralized architecture:
```text
            Host
          /  |  \
       Alice Bob Charlie
```
Clients send chat messages to the host. The host displays them locally and broadcasts them to the other connected clients.
The purpose of this project is to practice:
Berkeley socket lifecycle
TCP byte-stream handling
`epoll`
non-blocking I/O
message framing
application-layer protocol design
per-connection state
partial reads and writes
connection and disconnection handling
Do not use third-party networking libraries.
---
Technical Constraints
Linux
C++17 or newer
TCP
Berkeley sockets
`epoll`
single-threaded event loop
non-blocking sockets
no Boost.Asio, libuv, or similar libraries
no Protobuf or other serialization framework
---
Suggested Command-Line Interface
The exact interface is your choice, but the program should support behavior similar to the following.
Host mode
```text
chat --name Bob --listen 8080
```
The program:
uses `Bob` as the host's display name
listens for incoming connections on TCP port `8080`
accepts multiple simultaneous clients
participates in the chatroom locally
Client mode
```text
chat --name Alice --connect 192.168.1.20:8080
```
The program:
uses `Alice` as the client's display name
connects to the specified host and port
performs the application-layer handshake
joins the chatroom
---
Architecture
Host
The host maintains:
one listening socket
one `epoll` instance
zero or more connected client sockets
state associated with every client
a receive buffer for every client
an outgoing buffer for every client, if required
the name of every successfully initialized client
The host should use one event loop:
```text
while running:

    wait for events

    for each event:

        if listening socket is ready:
            accept new connections

        else if standard input is ready:
            read host input
            broadcast it

        else:
            process activity from a client socket
```
Client
The client maintains:
one TCP connection to the host
one `epoll` instance
the host socket
standard input
receive buffering
outgoing buffering, if required
connection-handshake state
The client should also use one event loop.
---
Socket Setup
Listening socket
The host must:
create a TCP socket
configure it as non-blocking
bind it to the requested port
place it into listening state
register it with `epoll`
Look into:
`socket()`
`bind()`
`listen()`
`fcntl()`
`O_NONBLOCK`
`epoll_create1()`
`epoll_ctl()`
Consider whether `SO_REUSEADDR` is useful during development.
Look into:
`setsockopt()`
`SO_REUSEADDR`
Outgoing connection
The client must:
create a TCP socket
configure it as non-blocking
initiate a connection to the host
register the socket with `epoll`
determine when the connection succeeds or fails
Look into:
`connect()`
`EINPROGRESS`
writable socket readiness
`getsockopt()`
`SO_ERROR`
Do not assume a non-blocking `connect()` completes immediately.
---
Event Loop
Use `epoll` to wait for:
new incoming connections
data available from connected sockets
sockets becoming writable
peer disconnections
terminal input from standard input
Look into:
`epoll_wait()`
`EPOLLIN`
`EPOLLOUT`
`EPOLLERR`
`EPOLLHUP`
`EPOLLRDHUP`
Standard input is file descriptor `0` and can be registered with `epoll`.
The event loop must not block while handling one connection.
---
Application Protocol
TCP provides an ordered byte stream, not messages.
The application must define its own framing protocol.
Use the following frame format:
```text
+----------------------+--------------------------+
| Message Type: 1 byte | Payload Length: 4 bytes |
+----------------------+--------------------------+
| Payload: N bytes                                |
+-------------------------------------------------+
```
Header
Message type
The first byte identifies the meaning of the frame.
Suggested message types:
```text
CLIENT_HELLO
SERVER_HELLO
CHAT
BROADCAST
ERROR
```
Assign explicit numeric values to these types.
Do not rely on compiler-specific enum representation unless you serialize it deliberately.
Payload length
The next four bytes contain an unsigned 32-bit integer representing the number of payload bytes.
Look into:
`uint32_t`
`<cstdint>`
network byte order
`htonl()`
`ntohl()`
The payload length does not include:
the one-byte message type
the four-byte length field
a null terminator
Payload
The payload contains exactly the number of bytes specified by the length field.
For ordinary text, use the bytes contained in the `std::string`.
For example:
```text
Payload: Alice
Length: 5
```
Do not transmit the terminating `'\0'`.
A `std::string` already tracks its own length. Use its length rather than treating it as a null-terminated C string.
---
Maximum Frame Size
Define a maximum permitted payload size.
For example:
```text
64 KiB
```
Reject or disconnect a peer that advertises a payload larger than the maximum.
This prevents malformed or malicious peers from forcing unreasonable memory allocation.
---
Handshake
The application-layer handshake begins only after the TCP connection has been established.
Client-to-server handshake
The client sends:
```text
CLIENT_HELLO
Payload: client's chosen name
```
Example:
```text
Type: CLIENT_HELLO
Length: 5
Payload: Alice
```
Server response
If the name is accepted, the server sends:
```text
SERVER_HELLO
Payload: server's name
```
Example:
```text
Type: SERVER_HELLO
Length: 3
Payload: Bob
```
After this exchange, the connection becomes active.
A separate final acknowledgement is not required for the first version. TCP already guarantees ordered and reliable delivery, and receiving `SERVER_HELLO` is enough for the client to know that the server accepted its introduction.
Invalid handshake
The server should reject a connection if:
the first complete frame is not `CLIENT_HELLO`
the name is empty
the name is too long
the frame is malformed
another active user already has the same name, if duplicate names are prohibited
The server may send:
```text
ERROR
Payload: human-readable reason
```
before closing the connection.
---
Connection State
Every connected socket must have application-level state.
Suggested states include:
Incoming server-side connection
```text
WAITING_FOR_CLIENT_HELLO
ACTIVE
CLOSING
```
Outgoing client-side connection
```text
CONNECTING
WAITING_FOR_SERVER_HELLO
ACTIVE
CLOSING
```
The meaning of a received frame depends partly on the connection's current state.
For example:
`CLIENT_HELLO` is valid while the server is waiting for the introduction
`CHAT` is valid only after the connection is active
`SERVER_HELLO` is valid while the client is waiting for the server response
A handshake message is the first complete framed protocol message, not necessarily the contents of the first `read()` call.
---
Per-Connection Data
Maintain data associated with each connected peer.
A connection record will likely need to contain information such as:
```text
socket descriptor
peer name
connection state
receive buffer
outgoing buffer
current parsing progress
current writing progress
```
Choose an appropriate C++ data structure for mapping a socket descriptor to its connection state.
Look into:
associative containers
object lifetime
ownership
iterator invalidation
removing state during event processing
Do not rely on the file descriptor alone as the user's identity.
---
Receiving Data
A readable socket means that a read operation can make progress without blocking.
It does not mean that one complete protocol frame is available.
A single read may contain:
only part of the five-byte header
a complete header but only part of the payload
exactly one complete frame
several complete frames
several frames plus part of another frame
The receiver must:
repeatedly read available bytes until no more can be read immediately
append the bytes to that connection's receive buffer
parse as many complete frames as possible
leave any incomplete frame in the buffer for a later event
Look into:
`recv()`
`read()`
`EAGAIN`
`EWOULDBLOCK`
`EINTR`
receive buffering
incremental parsing
Do not treat `EAGAIN` or `EWOULDBLOCK` as a connection failure.
---
Frame Parsing
A frame may be processed only when:
at least five bytes of header are available
the payload length has been decoded
the payload length is valid
the complete payload is available
Conceptually:
```text
while buffer contains a complete frame:

    inspect message type

    decode payload length

    validate payload length

    wait if the complete payload has not arrived

    extract frame

    process frame

    remove consumed bytes
```
The parser must preserve unconsumed bytes.
Avoid assuming messages align with TCP packets or read calls.
---
Sending Data
A call to `send()` may transmit fewer bytes than requested.
This can happen even when the socket is functioning normally.
The program must not assume:
```text
one send call = one complete frame sent
```
Serialize each outgoing frame into bytes and track how many bytes have already been transmitted.
If the complete output cannot be written immediately:
save the remaining bytes in the connection's outgoing buffer
register interest in writable events
resume sending when the socket becomes writable
stop watching writable events when the outgoing buffer becomes empty
Look into:
`send()`
partial writes
`EAGAIN`
`EWOULDBLOCK`
`EPOLLOUT`
output queues
Avoid registering `EPOLLOUT` permanently. A socket is writable most of the time, which can cause the event loop to wake continuously.
---
Chat Messages
Client-to-server chat
An active client sends:
```text
CHAT
Payload: text typed by the user
```
The payload should contain only the chat text.
The client should not be trusted to provide the sender's display name. The server already knows the name associated with the connection.
Server-side handling
When the server receives a valid `CHAT` frame from an active client:
identify the sender using the connection record
display the message locally
produce a broadcast representation
send it to every other active client
Example local display:
```text
[Alice] Hello everyone
```
Server-to-client broadcast
The simplest first version may send:
```text
BROADCAST
Payload: Alice: Hello everyone
```
This is acceptable for the first implementation.
A later version may use a structured broadcast payload containing:
```text
sender-name length
sender-name
message text
```
Do not add that complexity until the basic chat works.
Host input
Text typed by the host should also appear in the room.
The host may broadcast:
```text
BROADCAST
Payload: Bob: Hello everyone
```
The host does not need to send a `CHAT` frame to itself.
---
Standard Input
Register standard input with `epoll`.
When standard input becomes readable:
read a line
remove the trailing newline if appropriate
interpret commands if commands are supported
otherwise treat it as a chat message
Possible commands:
```text
/quit
/list
```
Only `/quit` is required if you choose to implement commands.
Be aware that terminal input behavior differs from socket behavior. For the initial version, line-oriented input is sufficient.
Look into:
standard input file descriptor
terminal canonical mode
reading lines without introducing a second thread
---
Accepting Connections
When the listening socket becomes readable, there may be more than one completed connection waiting.
The host should continue accepting until no more connections can be accepted immediately.
Look into:
`accept()`
`accept4()`
`SOCK_NONBLOCK`
`EAGAIN`
`EWOULDBLOCK`
For every accepted socket:
make it non-blocking
create its connection state
mark it as waiting for `CLIENT_HELLO`
register it with `epoll`
Do not send `SERVER_HELLO` until a valid client introduction has been received.
---
Disconnection Handling
A peer may disconnect normally or unexpectedly.
Possible indicators include:
`read()` or `recv()` returns `0`
`EPOLLRDHUP`
`EPOLLHUP`
unrecoverable socket error
failed write
malformed protocol input
When disconnecting a peer:
optionally announce the departure
remove the descriptor from `epoll`
close the socket
remove its connection record
discard its receive and outgoing buffers
ensure no later event processing refers to deleted state
Example server display:
```text
Alice disconnected
```
The server may optionally broadcast:
```text
SERVER_NOTICE
Payload: Alice left the room
```
This is not required for the first version.
---
Error Handling
Distinguish between:
Retryable conditions
Examples:
```text
EAGAIN
EWOULDBLOCK
EINTR
EINPROGRESS
```
These do not necessarily indicate connection failure.
Permanent errors
Examples include:
connection reset
broken pipe
invalid protocol frame
failed connection attempt
unrecoverable `epoll` error
Look into:
`errno`
`strerror()`
`SIGPIPE`
`MSG_NOSIGNAL`
Prevent the process from terminating unexpectedly when writing to a socket whose peer has disconnected.
---
Duplicate Names
Choose and document one policy.
Recommended policy:
```text
Active display names must be unique.
```
If a new client requests a name already in use:
send an `ERROR` frame
optionally flush the error if practical
close the connection
For the first milestone, duplicate names may temporarily be allowed, but the final version should have a clear rule.
---
Required Behavior
A completed implementation should support this scenario:
Bob starts a host on port `8080`.
Alice connects to Bob.
Alice sends `CLIENT_HELLO("Alice")`.
Bob responds with `SERVER_HELLO("Bob")`.
Charlie connects and completes the same handshake.
Alice types a message.
Bob displays Alice's message locally.
Charlie receives and displays Alice's message.
Bob types a message.
Alice and Charlie both receive it.
Charlie disconnects.
Bob cleans up Charlie's socket without affecting Alice.
Alice continues chatting normally.
---
Suggested Development Milestones
Milestone 1: Blocking connection test
Create a minimal host and client that can establish one TCP connection.
Look into:
`socket()`
`bind()`
`listen()`
`accept()`
`connect()`
Do not add the full protocol yet.
Milestone 2: Host event loop
Create an `epoll` instance.
Register:
the listening socket
standard input
Accept and track multiple clients.
Print connection and disconnection events.
Milestone 3: Non-blocking client sockets
Make accepted and outgoing sockets non-blocking.
Correctly handle:
`EAGAIN`
`EWOULDBLOCK`
`EINPROGRESS`
Milestone 4: Frame serialization
Implement creation of:
```text
type + length + payload
```
Verify serialized frame bytes manually or with focused tests.
Milestone 5: Incremental parser
Implement per-connection buffering.
Verify:
fragmented headers
fragmented payloads
multiple frames in one read
incomplete final frames
Milestone 6: Handshake
Implement:
```text
CLIENT_HELLO
SERVER_HELLO
ERROR
```
Track application-level connection state.
Milestone 7: Chatroom behavior
Implement:
```text
CHAT
BROADCAST
```
Broadcast client messages through the host.
Include host input in the room.
Milestone 8: Partial writes
Add outgoing queues.
Enable and disable `EPOLLOUT` dynamically.
Milestone 9: Cleanup and malformed input
Handle:
normal EOF
resets
invalid message types
oversized payload lengths
messages sent in the wrong connection state
duplicate names
attempted writes to disconnected peers
---
Testing Requirements
Test locally before using two physical machines.
Multiple instances can run in separate terminals on the same computer using:
```text
127.0.0.1
```
Test at least the following cases.
Connection tests
one client connects
several clients connect
client connects before sending a name
client disconnects during handshake
connection to an unused port fails cleanly
Framing tests
header arrives in pieces
payload arrives in pieces
several messages arrive together
one and a half messages arrive together
zero-length payload
oversized payload
invalid message type
Chat tests
host sends a message
one client sends a message
several clients exchange messages
sender does not receive an unwanted duplicate, according to your chosen policy
disconnected users are removed
Robustness tests
client closes abruptly
host closes
user enters multiple messages quickly
one peer stops reading
one peer sends malformed input
file descriptor is cleaned up exactly once
Consider writing a small protocol test program or unit tests for serialization and parsing independently from the networking code.
---
Design Questions to Resolve Before Coding
Document your answers to these questions:
What C++ type represents a connection?
How are connections mapped from file descriptor to state?
How are incoming and outgoing connections distinguished?
How does the event loop distinguish the listening socket, standard input, and peer sockets?
What exact numeric values represent each protocol message type?
What is the maximum username length?
What is the maximum chat-message length?
Are duplicate usernames allowed?
Does the sender receive its own message back from the server?
How are partial frames retained?
How are partial writes retained?
When is `EPOLLOUT` enabled and disabled?
What happens when a client sends `CHAT` before completing the handshake?
What cleanup function or mechanism owns socket removal?
How will stale events be prevented from accessing deleted connection state?
---
Non-Goals
The first version does not need:
encryption
authentication
account registration
NAT traversal
peer discovery
decentralized peer-to-peer routing
persistent message history
file transfer
reconnection
graphical interface
Protobuf
IPv6
multiple server processes
multiple event-loop threads
These may be explored only after the core implementation works reliably.
---
Completion Criteria
The project is complete when:
the host can accept several clients
every connection performs the name handshake
the server and clients learn each other's names
TCP frames are reconstructed correctly across arbitrary read boundaries
clients can send chat messages
the host broadcasts messages to other active clients
the host can participate in the room
partial reads are handled
partial writes are handled
all sockets are non-blocking
`epoll` drives the event loop
disconnects do not crash or freeze the application
malformed or oversized frames are rejected safely
connection state and resources are cleaned up correctly
The main learning objective is not the chat interface itself. It is building a correct mental and practical model of an event-driven TCP service.
