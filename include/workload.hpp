#pragma once

#include "types.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace titan
{

    enum class ActionType : uint8_t
    {
        NewOrder = 0,
        CancelOrder = 1
    };

    struct OrderAction
    {
        ActionType type;
        Order order;       // if type == NewOrder
        OrderId cancel_id; // if type == CancelOrder
    };

    enum class WorkloadType
    {
        Balanced,    // ~50% buys, 50% sells, normal spread crossing
        InsertHeavy, // 80% inserts, 10% cancels, 10% crosses
        CancelHeavy  // 40% inserts, 50% cancels, 10% crosses
    };

    class WorkloadGenerator
    {
    public:
        static std::vector<OrderAction> generate(
            WorkloadType type,
            size_t count,
            uint64_t seed = 42,
            Price mid_price = 10000, // ₹100.00
            Price spread_half = 50   // ₹0.50 spread range
        )
        {
            std::mt19937_64 rng(seed);
            std::vector<OrderAction> actions;
            actions.reserve(count);

            std::vector<OrderId> active_order_ids;
            active_order_ids.reserve(count / 2);

            // Distributions
            std::uniform_int_distribution<int> pct_dist(0, 99);
            std::normal_distribution<double> price_offset_dist(0.0, static_cast<double>(spread_half));
            std::uniform_int_distribution<uint32_t> qty_dist(10, 500);
            std::uniform_int_distribution<int> side_dist(0, 1);

            OrderId next_id = 1;

            int new_pct = 50;
            int cancel_pct = 25;

            if (type == WorkloadType::InsertHeavy)
            {
                new_pct = 80;
                cancel_pct = 10;
            }
            else if (type == WorkloadType::CancelHeavy)
            {
                new_pct = 40;
                cancel_pct = 50;
            }

            for (size_t i = 0; i < count; ++i)
            {
                int roll = pct_dist(rng);

                if (roll < new_pct || active_order_ids.empty())
                {
                    // generate new order
                    Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
                    double offset = price_offset_dist(rng);

                    int64_t raw_price = static_cast<int64_t>(mid_price) + static_cast<int64_t>(std::round(offset));
                    if (raw_price < 100)
                        raw_price = 100; // min price ₹1.00
                    Price price = static_cast<Price>(raw_price);
                    Quantity qty = qty_dist(rng);

                    OrderId oid = next_id++;
                    Order ord{
                        .order_id = oid,
                        .price = price,
                        .initial_quantity = qty,
                        .remaining_quantity = qty,
                        .timestamp = static_cast<Timestamp>(i * 100),
                        .side = side,
                        .type = OrderType::Limit};

                    actions.push_back(OrderAction{
                        .type = ActionType::NewOrder,
                        .order = ord,
                        .cancel_id = 0});

                    active_order_ids.push_back(oid);
                }
                else if (roll < (new_pct + cancel_pct))
                {
                    // gen cancel
                    std::uniform_int_distribution<size_t> idx_dist(0, active_order_ids.size() - 1);
                    size_t rand_idx = idx_dist(rng);
                    OrderId to_cancel = active_order_ids[rand_idx];

                    // fast swap and pop removal from active pool
                    active_order_ids[rand_idx] = active_order_ids.back();
                    active_order_ids.pop_back();

                    actions.push_back(OrderAction{
                        .type = ActionType::CancelOrder,
                        .order = {},
                        .cancel_id = to_cancel});
                }
                else
                {
                    // Generate aggressice market cross
                    Side side = (side_dist(rng) == 0) ? Side::Buy : Side::Sell;
                    Quantity qty = qty_dist(rng) / 2; // smaller market sweeps
                    if (qty == 0)
                        qty = 10;

                    OrderId oid = next_id++;
                    Order ord{
                        .order_id = oid,
                        .price = 0,
                        .initial_quantity = qty,
                        .remaining_quantity = qty,
                        .timestamp = static_cast<Timestamp>(i * 100),
                        .side = side,
                        .type = OrderType::Market};

                    actions.push_back(OrderAction{
                        .type = ActionType::NewOrder,
                        .order = ord,
                        .cancel_id = 0});
                }
            }

            return actions;
        }
    };

    struct BenchmarkStats
    {
        double total_time_ms{0.0};
        double ops_per_sec{0.0};
        double p50_ns{0.0};
        double p95_ns{0.0};
        double p99_ns{0.0};
        double p99_9_ns{0.0};
        double max_ns{0.0};
        double mean_ns{0.0};
        size_t total_trades{0};
        size_t total_orders_in_book{0};
    };

    inline BenchmarkStats calculate_stats(std::vector<uint64_t> &latencies_ns, double total_time_ms, size_t trade_count, size_t final_book_orders)
    {
        BenchmarkStats stats;
        stats.total_time_ms = total_time_ms;
        stats.total_trades = trade_count;
        stats.total_orders_in_book = final_book_orders;

        if (latencies_ns.empty())
            return stats;

        std::sort(latencies_ns.begin(), latencies_ns.end());

        size_t n = latencies_ns.size();
        stats.ops_per_sec = (static_cast<double>(n) / (total_time_ms / 1000.0));

        stats.p50_ns = latencies_ns[static_cast<size_t>(n * 0.50)];
        stats.p95_ns = latencies_ns[static_cast<size_t>(n * 0.95)];
        stats.p99_ns = latencies_ns[static_cast<size_t>(n * 0.99)];
        stats.p99_9_ns = latencies_ns[static_cast<size_t>(n * 0.999)];
        stats.max_ns = latencies_ns.back();

        uint64_t sum = std::accumulate(latencies_ns.begin(), latencies_ns.end(), uint64_t{0});
        stats.mean_ns = static_cast<double>(sum) / n;

        return stats;
    }

}
