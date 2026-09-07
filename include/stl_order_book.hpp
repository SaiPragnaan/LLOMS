#pragma once

#include "types.hpp"

#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace titan
{

    class StlOrderBook
    {
    public:
        StlOrderBook() = default;
        ~StlOrderBook() = default;

        StlOrderBook(const StlOrderBook &) = delete;
        StlOrderBook &operator=(const StlOrderBook &) = delete;
        StlOrderBook(StlOrderBook &&) noexcept = default;
        StlOrderBook &operator=(StlOrderBook &&) noexcept = default;

        bool submit_order(Order order, std::vector<Trade> &out_trades);

        bool cancel_order(OrderId order_id);

        [[nodiscard]] bool has_order(OrderId order_id) const noexcept;
        [[nodiscard]] std::optional<Order> get_order(OrderId order_id) const;
        [[nodiscard]] std::optional<Price> best_bid() const noexcept;
        [[nodiscard]] std::optional<Price> best_ask() const noexcept;
        [[nodiscard]] size_t total_orders() const noexcept;
        [[nodiscard]] size_t bid_levels() const noexcept;
        [[nodiscard]] size_t ask_levels() const noexcept;
        [[nodiscard]] Quantity volume_at_price(Side side, Price price) const;

        void clear() noexcept;

    private:
        struct OrderLocation
        {
            Side side;
            Price price;
            std::list<Order>::iterator list_it;
        };

        std::map<Price, std::list<Order>, std::greater<Price>> bids_;

        std::map<Price, std::list<Order>, std::less<Price>> asks_;

        std::unordered_map<OrderId, OrderLocation> order_index_;

        void match_buy(Order &incoming_buy, std::vector<Trade> &out_trades);
        void match_sell(Order &incoming_sell, std::vector<Trade> &out_trades);
    };

}
