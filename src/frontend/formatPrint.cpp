#include "formatPrint.hpp"

#include <iostream>

void format_print_message(const std::string &name, const Message &message) {
  std::cout << "[" << name << "] " << message.payload << std::endl;
}

/**
 * Creates a message of type BROADCAST with the formatted message.
 */
Message format_broadcast_message(const std::string &name,
                                 const Message &message) {
  Message broadcast{};
  broadcast.type = BROADCAST;
  broadcast.payload = "[" + name + "] " + message.payload;
  broadcast.payload_length = static_cast<uint32_t>(broadcast.payload.size());
  return broadcast;
}
