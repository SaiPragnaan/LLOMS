#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace titan
{

    using OrderId = uint64_t;
    using Price = uint64_t; // Stored in ticks (e.g. 101.25 -> 10125 for tick_size 0.01)
    using Quantity = uint32_t;
    using Timestamp = uint64_t; // nano secs

    enum class Side : uint8_t
    {
        Buy = 0,
        Sell = 1
    };

    enum class OrderType : uint8_t
    {
        Limit = 0,
        Market = 1
    };

    struct Order
    {
        OrderId order_id{0};
        Price price{0};
        Quantity initial_quantity{0};
        Quantity remaining_quantity{0};
        Timestamp timestamp{0};
        Side side{Side::Buy};
        OrderType type{OrderType::Limit};

        bool is_filled() const noexcept
        {
            return remaining_quantity == 0;
        }
    };

    struct Trade
    {
        OrderId maker_order_id{0};
        OrderId taker_order_id{0};
        Price price{0}; // exec price -- rest order price
        Quantity quantity{0};
        Timestamp timestamp{0};
        Side aggressor_side{Side::Buy};
    };

    inline constexpr std::string_view side_to_string(Side side) noexcept
    {
        return side == Side::Buy ? "BUY" : "SELL";
    }

    inline constexpr std::string_view order_type_to_string(OrderType type) noexcept
    {
        return type == OrderType::Limit ? "LIMIT" : "MARKET";
    }

}