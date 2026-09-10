#pragma once

#include "protocol.hpp"
#include "types.hpp"

#include <cstdint>
#include <string>

namespace titan {

struct IngressMessage {
    uint64_t sequence{0};
    int client_fd{-1};
    Timestamp ingress_timestamp{0};
    ParsedCommand command{};
};

struct ResponseMessage {
    int client_fd{-1};
    uint64_t sequence{0};
    std::string payload{};
};

} // namespace titan
