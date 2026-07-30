/**
 * @file protocol.cpp
 * @brief The application protocol for sending and receiving TCP messages.
 */

#include "protocol.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <optional>

#include "../log/log.h"

#define TAG &PROTOCOL_TAG

static constexpr uint32_t MAX_PAYLOAD_SIZE = 64 * 1024;
static constexpr size_t FRAME_HEADER_SIZE = 5;

bool protocol_payload_length_is_legal(size_t payload_length) {
  return payload_length <= MAX_PAYLOAD_SIZE;
}

/**
 * Queues a network formatted message to the connection's out_buffer.
 * @return true if the message was successfully queued, false if the payload is
 * too large.
 * @note Does not update EPOLL instance or send the message. The caller is
 * responsible for that.
 */
bool protocol_queue_message(Connection *conn, const Message &message) {
  if (!protocol_payload_length_is_legal(message.payload.size())) {
    logd(TAG, ERROR, "Payload too large: %zu bytes\n", message.payload.size());
    conn->state = CONNECTION_ERROR;
    return false;
  }

  uint32_t payload_length =
      htonl(static_cast<uint32_t>(message.payload.size()));

  // append message type: 1 byte
  conn->out_buffer.push_back(static_cast<char>(message.type));

  // append payload length: 4 bytes
  const char *length_bytes = reinterpret_cast<const char *>(&payload_length);
  conn->out_buffer.insert(conn->out_buffer.end(), length_bytes,
                          length_bytes + sizeof(payload_length));
  // append payload: payload_length bytes
  conn->out_buffer.insert(conn->out_buffer.end(), message.payload.begin(),
                          message.payload.end());

  return true;
}

/**
 * Attempts to parse a message from the connection's in_buffer.
 * @return an optional Message. If the message is incomplete or illegal, returns
 * std::nullopt. If parsing succeedds, return parsed message.
 * @note If message is illegal, connection state is set to CONNECTION_ERROR.
 */
std::optional<Message> protocol_parse_message(Connection *conn) {
  // check if we received the whole header
  if (conn->in_buffer.size() < FRAME_HEADER_SIZE) {
    return std::nullopt;
  }

  // extract message type
  uint8_t raw_type = static_cast<uint8_t>(conn->in_buffer[0]);

  // extract and convert payload length
  uint32_t network_payload_length = 0;
  memcpy(&network_payload_length, conn->in_buffer.data() + 1,
         sizeof(network_payload_length));
  uint32_t payload_length = ntohl(network_payload_length);

  // verify legal payload length
  if (!protocol_payload_length_is_legal(payload_length)) {
    logd(TAG, ERROR, "Received oversized payload: %u bytes\n", payload_length);
    conn->state = CONNECTION_ERROR;
    return std::nullopt;
  }

  // check if we received the whole message
  size_t frame_size = FRAME_HEADER_SIZE + payload_length;
  if (conn->in_buffer.size() < frame_size) { // fragmented
    return std::nullopt;
  }

  // reconstruct message
  Message message{};
  message.type = static_cast<MessageType>(raw_type);
  message.payload_length = payload_length;
  message.payload.assign(conn->in_buffer.begin() + FRAME_HEADER_SIZE,
                         conn->in_buffer.begin() + frame_size);

  // clear message from buffer
  conn->in_buffer.erase(conn->in_buffer.begin(),
                        conn->in_buffer.begin() + frame_size);

  return message;
}
