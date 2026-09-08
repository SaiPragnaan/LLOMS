#pragma once

#include "types.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace titan {

class PoolOrderBook {
public:
    static constexpr uint32_t INVALID_IDX = std::numeric_limits<uint32_t>::max();

    struct PriceLevel;

    struct OrderNode {
        Order order;
        uint32_t prev_idx{INVALID_IDX};
        // When active: next order in price queue
        // When free: points to next available free slot in the intrusive free-list
        uint32_t next_idx{INVALID_IDX};
    };

    struct PriceLevel {
        Price price{0};
        Quantity total_volume{0};
        uint32_t order_count{0};
        uint32_t head_idx{INVALID_IDX};
        uint32_t tail_idx{INVALID_IDX};

        bool empty() const noexcept {
            return head_idx == INVALID_IDX;
        }
    };

    explicit PoolOrderBook(size_t initial_capacity = 1000000);
    ~PoolOrderBook() = default;

    PoolOrderBook(const PoolOrderBook&) = delete;
    PoolOrderBook& operator=(const PoolOrderBook&) = delete;
    PoolOrderBook(PoolOrderBook&&) noexcept = default;
    PoolOrderBook& operator=(PoolOrderBook&&) noexcept = default;

    bool submit_order(Order order, std::vector<Trade>& out_trades);
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

    [[nodiscard]] size_t pool_capacity() const noexcept { return nodes_.size(); }

private:
    // pool memory slots for Order Nodes
    std::vector<OrderNode> nodes_;
    
    // Free-List head: points to first free slot directly inside nodes_
    uint32_t free_head_{INVALID_IDX};


    std::unordered_map<OrderId, uint32_t> order_index_;

    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel, std::less<Price>> asks_;



    uint32_t allocate_node(const Order& order);
    void free_node(uint32_t idx);



    void append_to_level(PriceLevel& level, uint32_t node_idx);
    void remove_from_level(PriceLevel& level, uint32_t node_idx);


    void match_buy(Order& incoming_buy, std::vector<Trade>& out_trades);
    void match_sell(Order& incoming_sell, std::vector<Trade>& out_trades);
};

}
