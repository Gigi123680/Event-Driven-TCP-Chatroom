#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "../connection/connection.hpp"

enum MessageType : uint8_t {
  CLIENT_HELLO = 1,
  SERVER_HELLO = 2,
  CHAT = 3,
  BROADCAST = 4,
  ERROR_MESSAGE = 5,
  CLOSE_CON = 6
};

typedef struct Message {
  MessageType type;
  uint32_t payload_length;
  std::string payload;
} Message;

bool protocol_queue_message(Connection *conn, const Message &message);
std::optional<Message> protocol_parse_message(Connection *conn);
bool protocol_payload_length_is_legal(size_t payload_length);
