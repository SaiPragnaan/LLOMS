#include "pool_order_book.hpp"
#include "stl_order_book.hpp"
#include "workload.hpp"

#include <cassert>
#include <iostream>
#include <vector>

#define ASSERT_TRUE(cond)                                                                                  \
    do                                                                                                     \
    {                                                                                                      \
        if (!(cond))                                                                                       \
        {                                                                                                  \
            std::cerr << "Assertion FAILED at " << __FILE__ << ":" << __LINE__ << " -> " << #cond << "\n"; \
            std::exit(1);                                                                                  \
        }                                                                                                  \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

void test_pool_unit_tests()
{
    std::cout << "[TEST] Running PoolOrderBook basic unit tests...\n";
    titan::PoolOrderBook book(1000);
    std::vector<titan::Trade> trades;

    // Resting orders
    book.submit_order(titan::Order{1, 10000, 100, 100, 1000, titan::Side::Buy, titan::OrderType::Limit}, trades);
    book.submit_order(titan::Order{2, 10100, 50, 50, 2000, titan::Side::Sell, titan::OrderType::Limit}, trades);
    ASSERT_EQ(book.total_orders(), 2);
    ASSERT_EQ(book.best_bid().value(), 10000);
    ASSERT_EQ(book.best_ask().value(), 10100);

    // Cancel order 1
    ASSERT_TRUE(book.cancel_order(1));
    ASSERT_FALSE(book.has_order(1));
    ASSERT_FALSE(book.best_bid().has_value());
    ASSERT_EQ(book.total_orders(), 1);

    // Market Buy sweeping the remaining sell
    trades.clear();
    book.submit_order(titan::Order{3, 0, 50, 50, 3000, titan::Side::Buy, titan::OrderType::Market}, trades);
    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(trades[0].maker_order_id, 2);
    ASSERT_EQ(trades[0].taker_order_id, 3);
    ASSERT_EQ(trades[0].quantity, 50);
    ASSERT_EQ(trades[0].price, 10100);
    ASSERT_EQ(book.total_orders(), 0);
}

void test_differential_fuzzing(size_t order_count, uint64_t seed)
{
    std::cout << "[TEST] Running Differential Testing: StlOrderBook vs PoolOrderBook ("
              << order_count << " orders, seed=" << seed << ")...\n";

    titan::StlOrderBook stl_book;
    titan::PoolOrderBook pool_book(order_count);

    auto actions = titan::WorkloadGenerator::generate(titan::WorkloadType::Balanced, order_count, seed);

    std::vector<titan::Trade> stl_trades;
    std::vector<titan::Trade> pool_trades;

    for (size_t i = 0; i < actions.size(); ++i)
    {
        const auto &action = actions[i];

        if (action.type == titan::ActionType::NewOrder)
        {
            bool stl_res = stl_book.submit_order(action.order, stl_trades);
            bool pool_res = pool_book.submit_order(action.order, pool_trades);

            ASSERT_EQ(stl_res, pool_res);
        }
        else
        {
            bool stl_res = stl_book.cancel_order(action.cancel_id);
            bool pool_res = pool_book.cancel_order(action.cancel_id);

            ASSERT_EQ(stl_res, pool_res);
        }

        // Verify trade equality step by step
        ASSERT_EQ(stl_trades.size(), pool_trades.size());
        if (!stl_trades.empty())
        {
            const auto &t_stl = stl_trades.back();
            const auto &t_pool = pool_trades.back();

            ASSERT_EQ(t_stl.maker_order_id, t_pool.maker_order_id);
            ASSERT_EQ(t_stl.taker_order_id, t_pool.taker_order_id);
            ASSERT_EQ(t_stl.price, t_pool.price);
            ASSERT_EQ(t_stl.quantity, t_pool.quantity);
            ASSERT_EQ(t_stl.aggressor_side, t_pool.aggressor_side);
        }

        // Verify book invariants match
        ASSERT_EQ(stl_book.total_orders(), pool_book.total_orders());
        ASSERT_EQ(stl_book.best_bid().has_value(), pool_book.best_bid().has_value());
        if (stl_book.best_bid().has_value())
        {
            ASSERT_EQ(stl_book.best_bid().value(), pool_book.best_bid().value());
        }

        ASSERT_EQ(stl_book.best_ask().has_value(), pool_book.best_ask().has_value());
        if (stl_book.best_ask().has_value())
        {
            ASSERT_EQ(stl_book.best_ask().value(), pool_book.best_ask().value());
        }
    }

    std::cout << "[PASS] Differential test passed! Generated " << stl_trades.size()
              << " identical trades across " << order_count << " operations.\n";
}

int main()
{
    std::cout << "===================================================\n";
    std::cout << " Running Differential & Invariant Testing Harness  \n";
    std::cout << "===================================================\n";

    test_pool_unit_tests();
    test_differential_fuzzing(10000, 42);
    test_differential_fuzzing(50000, 1337);

    std::cout << "===================================================\n";
    std::cout << " ALL DIFFERENTIAL TESTS PASSED WITH 100% EQUALITY! \n";
    std::cout << "===================================================\n";
    return 0;
}
