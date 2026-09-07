#include "stl_order_book.hpp"
#include <algorithm>

namespace titan
{

    bool StlOrderBook::submit_order(Order order, std::vector<Trade> &out_trades)
    {
        if (order.initial_quantity == 0)
        {
            return false;
        }
        if (order.remaining_quantity == 0)
        {
            order.remaining_quantity = order.initial_quantity;
        }
        if (order.type == OrderType::Limit && order.price == 0)
        {
            return false;
        }
        if (order_index_.find(order.order_id) != order_index_.end())
        {
            return false;
        }

        if (order.side == Side::Buy)
        {
            match_buy(order, out_trades);
        }
        else
        {
            match_sell(order, out_trades);
        }

        /*
        If there is leftover quantity on a Limit order, rest it in the book
        --- AS this is a new limit order it is being pushed at the back of the queue or list,
        if there is a scenario of partial fill in the existing order in the order book, it would be updated
        in place rather than pushing the modified order as a new entryh
        */
        if (order.remaining_quantity > 0 && order.type == OrderType::Limit)
        {
            if (order.side == Side::Buy)
            {
                auto &queue = bids_[order.price];
                queue.push_back(order);
                auto it = std::prev(queue.end());
                order_index_[order.order_id] = OrderLocation{Side::Buy, order.price, it};
            }
            else
            {
                auto &queue = asks_[order.price];
                queue.push_back(order);
                auto it = std::prev(queue.end());
                order_index_[order.order_id] = OrderLocation{Side::Sell, order.price, it};
            }
        }

        return true;
    }

    void StlOrderBook::match_buy(Order &incoming_buy, std::vector<Trade> &out_trades)
    {
        while (!asks_.empty() && incoming_buy.remaining_quantity > 0)
        {
            auto best_ask_it = asks_.begin();
            const Price best_ask_price = best_ask_it->first;

            if (incoming_buy.type == OrderType::Limit && incoming_buy.price < best_ask_price)
            {
                break; // Price does not cross the spread
            }

            auto &queue = best_ask_it->second;
            while (!queue.empty() && incoming_buy.remaining_quantity > 0)
            {
                Order &maker = queue.front();
                const Quantity match_qty = std::min(incoming_buy.remaining_quantity, maker.remaining_quantity);

                out_trades.push_back(Trade{
                    .maker_order_id = maker.order_id,
                    .taker_order_id = incoming_buy.order_id,
                    .price = maker.price,
                    .quantity = match_qty,
                    .timestamp = incoming_buy.timestamp,
                    .aggressor_side = Side::Buy});

                maker.remaining_quantity -= match_qty;
                incoming_buy.remaining_quantity -= match_qty;

                if (maker.is_filled())
                {
                    order_index_.erase(maker.order_id);
                    queue.pop_front();
                }
            }

            if (queue.empty())
            {
                asks_.erase(best_ask_it);
            }
        }
    }

    void StlOrderBook::match_sell(Order &incoming_sell, std::vector<Trade> &out_trades)
    {
        while (!bids_.empty() && incoming_sell.remaining_quantity > 0)
        {
            auto best_bid_it = bids_.begin();
            const Price best_bid_price = best_bid_it->first;

            if (incoming_sell.type == OrderType::Limit && incoming_sell.price > best_bid_price)
            {
                break; // Price does not cross the spread
            }

            auto &queue = best_bid_it->second;
            while (!queue.empty() && incoming_sell.remaining_quantity > 0)
            {
                Order &maker = queue.front();
                const Quantity match_qty = std::min(incoming_sell.remaining_quantity, maker.remaining_quantity);

                out_trades.push_back(Trade{
                    .maker_order_id = maker.order_id,
                    .taker_order_id = incoming_sell.order_id,
                    .price = maker.price,
                    .quantity = match_qty,
                    .timestamp = incoming_sell.timestamp,
                    .aggressor_side = Side::Sell});

                maker.remaining_quantity -= match_qty;
                incoming_sell.remaining_quantity -= match_qty;

                if (maker.is_filled())
                {
                    order_index_.erase(maker.order_id);
                    queue.pop_front();
                }
            }

            if (queue.empty())
            {
                bids_.erase(best_bid_it);
            }
        }
    }

    bool StlOrderBook::cancel_order(OrderId order_id)
    {
        auto it = order_index_.find(order_id);
        if (it == order_index_.end())
        {
            return false;
        }

        const OrderLocation loc = it->second;
        if (loc.side == Side::Buy)
        {
            auto map_it = bids_.find(loc.price);
            if (map_it != bids_.end())
            {
                map_it->second.erase(loc.list_it);
                if (map_it->second.empty())
                {
                    bids_.erase(map_it);
                }
            }
        }
        else
        {
            auto map_it = asks_.find(loc.price);
            if (map_it != asks_.end())
            {
                map_it->second.erase(loc.list_it);
                if (map_it->second.empty())
                {
                    asks_.erase(map_it);
                }
            }
        }

        order_index_.erase(it);
        return true;
    }

    bool StlOrderBook::has_order(OrderId order_id) const noexcept
    {
        return order_index_.find(order_id) != order_index_.end();
    }

    std::optional<Order> StlOrderBook::get_order(OrderId order_id) const
    {
        auto it = order_index_.find(order_id);
        if (it == order_index_.end())
        {
            return std::nullopt;
        }
        return *(it->second.list_it);
    }

    std::optional<Price> StlOrderBook::best_bid() const noexcept
    {
        if (bids_.empty())
            return std::nullopt;
        return bids_.begin()->first;
    }

    std::optional<Price> StlOrderBook::best_ask() const noexcept
    {
        if (asks_.empty())
            return std::nullopt;
        return asks_.begin()->first;
    }

    size_t StlOrderBook::total_orders() const noexcept
    {
        return order_index_.size();
    }

    size_t StlOrderBook::bid_levels() const noexcept
    {
        return bids_.size();
    }

    size_t StlOrderBook::ask_levels() const noexcept
    {
        return asks_.size();
    }

    Quantity StlOrderBook::volume_at_price(Side side, Price price) const
    {
        Quantity total = 0;
        if (side == Side::Buy)
        {
            auto it = bids_.find(price);
            if (it != bids_.end())
            {
                for (const auto &ord : it->second)
                {
                    total += ord.remaining_quantity;
                }
            }
        }
        else
        {
            auto it = asks_.find(price);
            if (it != asks_.end())
            {
                for (const auto &ord : it->second)
                {
                    total += ord.remaining_quantity;
                }
            }
        }
        return total;
    }

    void StlOrderBook::clear() noexcept
    {
        bids_.clear();
        asks_.clear();
        order_index_.clear();
    }

}
