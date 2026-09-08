#include "protocol.hpp"

#include <charconv>
#include <chrono>

namespace titan
{

    namespace
    {

        // Fast in-place tokenization on string_view without heap allocations
        std::vector<std::string_view> split_tokens(std::string_view line)
        {
            std::vector<std::string_view> tokens;
            size_t start = 0;
            while (start < line.size())
            {
                while (start < line.size() && (line[start] == ' ' || line[start] == '\t' || line[start] == '\r'))
                {
                    start++;
                }
                if (start >= line.size())
                    break;

                size_t end = start;
                while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '\r')
                {
                    end++;
                }

                tokens.push_back(line.substr(start, end - start));
                start = end;
            }
            return tokens;
        }

        template <typename T>
        bool parse_num(std::string_view sv, T &out)
        {
            auto res = std::from_chars(sv.data(), sv.data() + sv.size(), out);
            return (res.ec == std::errc{} && res.ptr == sv.data() + sv.size());
        }

    }

    ParsedCommand Protocol::parse_line(std::string_view line) noexcept
    {
        auto tokens = split_tokens(line);
        if (tokens.empty())
        {
            return ParsedCommand{.type = CommandType::Unknown, .error_msg = "EMPTY_COMMAND"};
        }

        std::string_view verb = tokens[0];

        if (verb == "NEW")
        {
            // Expected format: NEW <SIDE> <ORDER_ID> <PRICE> <QTY> [LIMIT|MARKET]
            if (tokens.size() < 5)
            {
                return ParsedCommand{
                    .type = CommandType::Unknown,
                    .error_msg = "INVALID_NEW_FORMAT: Expected 'NEW <BUY|SELL> <ID> <PRICE> <QTY> [TYPE]'"};
            }

            Side side = Side::Buy;
            if (tokens[1] == "BUY" || tokens[1] == "B")
            {
                side = Side::Buy;
            }
            else if (tokens[1] == "SELL" || tokens[1] == "S")
            {
                side = Side::Sell;
            }
            else
            {
                return ParsedCommand{.type = CommandType::Unknown, .error_msg = "INVALID_SIDE: Expected BUY or SELL"};
            }

            OrderId oid = 0;
            if (!parse_num(tokens[2], oid) || oid == 0)
            {
                return ParsedCommand{.type = CommandType::Unknown, .error_msg = "INVALID_ORDER_ID"};
            }

            Price price = 0;
            if (!parse_num(tokens[3], price))
            {
                return ParsedCommand{.type = CommandType::Unknown, .error_msg = "INVALID_PRICE"};
            }

            Quantity qty = 0;
            if (!parse_num(tokens[4], qty) || qty == 0)
            {
                return ParsedCommand{.type = CommandType::Unknown, .error_msg = "INVALID_QUANTITY"};
            }

            OrderType order_type = OrderType::Limit;
            if (tokens.size() >= 6)
            {
                if (tokens[5] == "MARKET" || tokens[5] == "M")
                {
                    order_type = OrderType::Market;
                }
                else if (tokens[5] == "LIMIT" || tokens[5] == "L")
                {
                    order_type = OrderType::Limit;
                }
            }

            auto now_ns = static_cast<Timestamp>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());

            Order ord{
                .order_id = oid,
                .price = price,
                .initial_quantity = qty,
                .remaining_quantity = qty,
                .timestamp = now_ns,
                .side = side,
                .type = order_type};

            return ParsedCommand{
                .type = CommandType::New,
                .order = ord,
                .cancel_id = 0,
                .error_msg = ""};
        }

        if (verb == "CANCEL")
        {
            // Expected format: CANCEL <ORDER_ID>
            if (tokens.size() < 2)
            {
                return ParsedCommand{
                    .type = CommandType::Unknown,
                    .error_msg = "INVALID_CANCEL_FORMAT: Expected 'CANCEL <ORDER_ID>'"};
            }

            OrderId oid = 0;
            if (!parse_num(tokens[1], oid) || oid == 0)
            {
                return ParsedCommand{.type = CommandType::Unknown, .error_msg = "INVALID_ORDER_ID"};
            }

            return ParsedCommand{
                .type = CommandType::Cancel,
                .order = {},
                .cancel_id = oid,
                .error_msg = ""};
        }

        if (verb == "FLUSH")
        {
            return ParsedCommand{.type = CommandType::Flush};
        }

        return ParsedCommand{.type = CommandType::Unknown, .error_msg = "UNKNOWN_COMMAND"};
    }

    void Protocol::format_ack(OrderId id, Quantity remaining_qty, std::string &out)
    {
        out += "ACK " + std::to_string(id) + " " + std::to_string(remaining_qty) + "\n";
    }

    void Protocol::format_trade(const Trade &trade, std::string &out)
    {
        out += "TRADE " + std::to_string(trade.maker_order_id) + " " + std::to_string(trade.taker_order_id) + " " + std::to_string(trade.price) + " " + std::to_string(trade.quantity) + " " + std::string(side_to_string(trade.aggressor_side)) + "\n";
    }

    void Protocol::format_canceled(OrderId id, std::string &out)
    {
        out += "CANCELED " + std::to_string(id) + "\n";
    }

    void Protocol::format_reject(OrderId id, std::string_view reason, std::string &out)
    {
        out += "REJECT " + std::to_string(id) + " " + std::string(reason) + "\n";
    }

    void Protocol::format_info(std::string_view message, std::string &out)
    {
        out += "INFO " + std::string(message) + "\n";
    }

}
