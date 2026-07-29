#pragma once

#include <string>

#include "../network/protocol.hpp"

void format_print_message(const std::string &name, const Message &message);
Message format_broadcast_message(const std::string &name,
                                 const Message &message);
