#include "pool_order_book.hpp"
#include <algorithm>

namespace titan {

PoolOrderBook::PoolOrderBook(size_t initial_capacity) {
    nodes_.resize(initial_capacity);
    order_index_.reserve(initial_capacity);

    // init free-list
    for (size_t i = 0; i < initial_capacity; ++i) {
        nodes_[i].prev_idx = INVALID_IDX;
        nodes_[i].next_idx = (i + 1 < initial_capacity) ? static_cast<uint32_t>(i + 1) : INVALID_IDX;
    }
    free_head_ = (initial_capacity > 0) ? 0 : INVALID_IDX;
}

uint32_t PoolOrderBook::allocate_node(const Order& order) {
    // If intrusive free-list is exhausted, expand capacity
    if (free_head_ == INVALID_IDX) {
        size_t old_size = nodes_.size();
        size_t new_size = (old_size == 0) ? 1024 : old_size * 2;
        nodes_.resize(new_size);

        for (size_t i = old_size; i < new_size; ++i) {
            nodes_[i].prev_idx = INVALID_IDX;
            nodes_[i].next_idx = (i + 1 < new_size) ? static_cast<uint32_t>(i + 1) : INVALID_IDX;
        }
        free_head_ = static_cast<uint32_t>(old_size);
    }

    // Pop from intrusive free-list head
    uint32_t idx = free_head_;
    free_head_ = nodes_[idx].next_idx;

    nodes_[idx].order = order;
    nodes_[idx].prev_idx = INVALID_IDX;
    nodes_[idx].next_idx = INVALID_IDX;
    return idx;
}

void PoolOrderBook::free_node(uint32_t idx) {
    // Push onto intrusive free-list head (LIFO - hot cache reuse)
    nodes_[idx].prev_idx = INVALID_IDX;
    nodes_[idx].next_idx = free_head_;
    free_head_ = idx;
}

void PoolOrderBook::append_to_level(PriceLevel& level, uint32_t node_idx) {
    nodes_[node_idx].prev_idx = level.tail_idx;
    nodes_[node_idx].next_idx = INVALID_IDX;

    if (level.tail_idx != INVALID_IDX) {
        nodes_[level.tail_idx].next_idx = node_idx;
    } else {
        level.head_idx = node_idx;
    }

    level.tail_idx = node_idx;
    level.total_volume += nodes_[node_idx].order.remaining_quantity;
    level.order_count++;
}

void PoolOrderBook::remove_from_level(PriceLevel& level, uint32_t node_idx) {
    uint32_t p = nodes_[node_idx].prev_idx;
    uint32_t n = nodes_[node_idx].next_idx;

    if (p != INVALID_IDX) {
        nodes_[p].next_idx = n;
    } else {
        level.head_idx = n;
    }

    if (n != INVALID_IDX) {
        nodes_[n].prev_idx = p;
    } else {
        level.tail_idx = p;
    }

    level.total_volume -= nodes_[node_idx].order.remaining_quantity;
    level.order_count--;
}

bool PoolOrderBook::submit_order(Order order, std::vector<Trade>& out_trades) {
    if (order.initial_quantity == 0) {
        return false;
    }
    if (order.remaining_quantity == 0) {
        order.remaining_quantity = order.initial_quantity;
    }
    if (order.type == OrderType::Limit && order.price == 0) {
        return false;
    }
    if (order_index_.find(order.order_id) != order_index_.end()) {
        return false; // Duplicate order_id rejection
    }

    if (order.side == Side::Buy) {
        match_buy(order, out_trades);
    } else {
        match_sell(order, out_trades);
    }

    // Rest remaining quantity in the book if Limit order
    if (order.remaining_quantity > 0 && order.type == OrderType::Limit) {
        uint32_t node_idx = allocate_node(order);
        order_index_[order.order_id] = node_idx;

        if (order.side == Side::Buy) {
            auto& level = bids_[order.price];
            level.price = order.price;
            append_to_level(level, node_idx);
        } else {
            auto& level = asks_[order.price];
            level.price = order.price;
            append_to_level(level, node_idx);
        }
    }

    return true;
}

void PoolOrderBook::match_buy(Order& incoming_buy, std::vector<Trade>& out_trades) {
    while (!asks_.empty() && incoming_buy.remaining_quantity > 0) {
        auto best_ask_it = asks_.begin();
        PriceLevel& level = best_ask_it->second;

        if (incoming_buy.type == OrderType::Limit && incoming_buy.price < level.price) {
            break; // Spread not crossed
        }

        while (level.head_idx != INVALID_IDX && incoming_buy.remaining_quantity > 0) {
            uint32_t maker_idx = level.head_idx;
            Order& maker = nodes_[maker_idx].order;
            const Quantity match_qty = std::min(incoming_buy.remaining_quantity, maker.remaining_quantity);

            out_trades.push_back(Trade{
                .maker_order_id = maker.order_id,
                .taker_order_id = incoming_buy.order_id,
                .price = maker.price,
                .quantity = match_qty,
                .timestamp = incoming_buy.timestamp,
                .aggressor_side = Side::Buy
            });

            maker.remaining_quantity -= match_qty;
            incoming_buy.remaining_quantity -= match_qty;

            if (maker.is_filled()) {
                order_index_.erase(maker.order_id);
                remove_from_level(level, maker_idx);
                free_node(maker_idx);
            }
        }

        if (level.empty()) {
            asks_.erase(best_ask_it);
        }
    }
}

void PoolOrderBook::match_sell(Order& incoming_sell, std::vector<Trade>& out_trades) {
    while (!bids_.empty() && incoming_sell.remaining_quantity > 0) {
        auto best_bid_it = bids_.begin();
        PriceLevel& level = best_bid_it->second;

        if (incoming_sell.type == OrderType::Limit && incoming_sell.price > level.price) {
            break; // Spread not crossed
        }

        while (level.head_idx != INVALID_IDX && incoming_sell.remaining_quantity > 0) {
            uint32_t maker_idx = level.head_idx;
            Order& maker = nodes_[maker_idx].order;
            const Quantity match_qty = std::min(incoming_sell.remaining_quantity, maker.remaining_quantity);

            out_trades.push_back(Trade{
                .maker_order_id = maker.order_id,
                .taker_order_id = incoming_sell.order_id,
                .price = maker.price,
                .quantity = match_qty,
                .timestamp = incoming_sell.timestamp,
                .aggressor_side = Side::Sell
            });

            maker.remaining_quantity -= match_qty;
            incoming_sell.remaining_quantity -= match_qty;

            if (maker.is_filled()) {
                order_index_.erase(maker.order_id);
                remove_from_level(level, maker_idx);
                free_node(maker_idx);
            }
        }

        if (level.empty()) {
            bids_.erase(best_bid_it);
        }
    }
}

bool PoolOrderBook::cancel_order(OrderId order_id) {
    auto it = order_index_.find(order_id);
    if (it == order_index_.end()) {
        return false;
    }

    uint32_t node_idx = it->second;
    const Order& ord = nodes_[node_idx].order;

    if (ord.side == Side::Buy) {
        auto lvl_it = bids_.find(ord.price);
        if (lvl_it != bids_.end()) {
            remove_from_level(lvl_it->second, node_idx);
            if (lvl_it->second.empty()) {
                bids_.erase(lvl_it);
            }
        }
    } else {
        auto lvl_it = asks_.find(ord.price);
        if (lvl_it != asks_.end()) {
            remove_from_level(lvl_it->second, node_idx);
            if (lvl_it->second.empty()) {
                asks_.erase(lvl_it);
            }
        }
    }

    free_node(node_idx);
    order_index_.erase(it);
    return true;
}

bool PoolOrderBook::has_order(OrderId order_id) const noexcept {
    return order_index_.find(order_id) != order_index_.end();
}

std::optional<Order> PoolOrderBook::get_order(OrderId order_id) const {
    auto it = order_index_.find(order_id);
    if (it == order_index_.end()) {
        return std::nullopt;
    }
    return nodes_[it->second].order;
}

std::optional<Price> PoolOrderBook::best_bid() const noexcept {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> PoolOrderBook::best_ask() const noexcept {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

size_t PoolOrderBook::total_orders() const noexcept {
    return order_index_.size();
}

size_t PoolOrderBook::bid_levels() const noexcept {
    return bids_.size();
}

size_t PoolOrderBook::ask_levels() const noexcept {
    return asks_.size();
}

Quantity PoolOrderBook::volume_at_price(Side side, Price price) const {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it != bids_.end()) {
            return it->second.total_volume;
        }
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end()) {
            return it->second.total_volume;
        }
    }
    return 0;
}

void PoolOrderBook::clear() noexcept {
    bids_.clear();
    asks_.clear();
    order_index_.clear();
    
    size_t cap = nodes_.size();
    for (size_t i = 0; i < cap; ++i) {
        nodes_[i].prev_idx = INVALID_IDX;
        nodes_[i].next_idx = (i + 1 < cap) ? static_cast<uint32_t>(i + 1) : INVALID_IDX;
    }
    free_head_ = (cap > 0) ? 0 : INVALID_IDX;
}

}
