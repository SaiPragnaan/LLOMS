#pragma once

#include "types.hpp"
#include "workload.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace titan
{

    enum class CommandType : uint8_t
    {
        New,
        Cancel,
        Flush,
        Unknown
    };

    struct ParsedCommand
    {
        CommandType type{CommandType::Unknown};
        Order order{};
        OrderId cancel_id{0};
        std::string error_msg{};
    };

    class Protocol
    {
    public:
        // Parses a single line (excluding trailing '\r' and '\n')
        // e.g. "NEW BUY 1001 10000 50 LIMIT" -> ParsedCommand
        // e.g. "CANCEL 1001" -> ParsedCommand
        static ParsedCommand parse_line(std::string_view line) noexcept;

        // Outbound serialization helpers
        static void format_ack(OrderId id, Quantity remaining_qty, std::string &out);
        static void format_trade(const Trade &trade, std::string &out);
        static void format_canceled(OrderId id, std::string &out);
        static void format_reject(OrderId id, std::string_view reason, std::string &out);
        static void format_info(std::string_view message, std::string &out);
    };

}
