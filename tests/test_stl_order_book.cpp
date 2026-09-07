#include "stl_order_book.hpp"
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

void test_basic_resting_orders()
{
    std::cout << "[TEST] Running test_basic_resting_orders...\n";
    titan::StlOrderBook book;
    std::vector<titan::Trade> trades;

    titan::Order buy1{
        .order_id = 1,
        .price = 10000,
        .initial_quantity = 100,
        .remaining_quantity = 100,
        .timestamp = 1000,
        .side = titan::Side::Buy,
        .type = titan::OrderType::Limit};
    ASSERT_TRUE(book.submit_order(buy1, trades));
    ASSERT_EQ(trades.size(), 0);
    ASSERT_EQ(book.total_orders(), 1);
    ASSERT_EQ(book.best_bid().value(), 10000);
    ASSERT_FALSE(book.best_ask().has_value());

    titan::Order sell1{
        .order_id = 2,
        .price = 10100,
        .initial_quantity = 50,
        .remaining_quantity = 50,
        .timestamp = 2000,
        .side = titan::Side::Sell,
        .type = titan::OrderType::Limit};
    ASSERT_TRUE(book.submit_order(sell1, trades));
    ASSERT_EQ(trades.size(), 0);
    ASSERT_EQ(book.total_orders(), 2);
    ASSERT_EQ(book.best_bid().value(), 10000);
    ASSERT_EQ(book.best_ask().value(), 10100);
}

void test_single_full_match()
{
    std::cout << "[TEST] Running test_single_full_match...\n";
    titan::StlOrderBook book;
    std::vector<titan::Trade> trades;

    book.submit_order(titan::Order{
                          .order_id = 10,
                          .price = 10000,
                          .initial_quantity = 100,
                          .remaining_quantity = 100,
                          .timestamp = 1000,
                          .side = titan::Side::Buy,
                          .type = titan::OrderType::Limit},
                      trades);

    book.submit_order(titan::Order{
                          .order_id = 11,
                          .price = 10000,
                          .initial_quantity = 100,
                          .remaining_quantity = 100,
                          .timestamp = 2000,
                          .side = titan::Side::Sell,
                          .type = titan::OrderType::Limit},
                      trades);

    ASSERT_EQ(trades.size(), 1);
    ASSERT_EQ(trades[0].maker_order_id, 10);
    ASSERT_EQ(trades[0].taker_order_id, 11);
    ASSERT_EQ(trades[0].price, 10000);
    ASSERT_EQ(trades[0].quantity, 100);
    ASSERT_EQ(trades[0].aggressor_side, titan::Side::Sell);

    ASSERT_EQ(book.total_orders(), 0);
    ASSERT_FALSE(book.best_bid().has_value());
    ASSERT_FALSE(book.best_ask().has_value());
}

void test_maker_price_priority_and_partial_fills()
{
    std::cout << "[TEST] Running test_maker_price_priority_and_partial_fills...\n";
    titan::StlOrderBook book;
    std::vector<titan::Trade> trades;

    book.submit_order(titan::Order{
                          .order_id = 1,
                          .price = 10000,
                          .initial_quantity = 100,
                          .remaining_quantity = 100,
                          .timestamp = 1000,
                          .side = titan::Side::Buy,
                          .type = titan::OrderType::Limit},
                      trades);

    book.submit_order(titan::Order{
                          .order_id = 2,
                          .price = 10050,
                          .initial_quantity = 50,
                          .remaining_quantity = 50,
                          .timestamp = 1100,
                          .side = titan::Side::Buy,
                          .type = titan::OrderType::Limit},
                      trades);

    ASSERT_EQ(book.best_bid().value(), 10050);

    book.submit_order(titan::Order{
                          .order_id = 3,
                          .price = 10000,
                          .initial_quantity = 80,
                          .remaining_quantity = 80,
                          .timestamp = 2000,
                          .side = titan::Side::Sell,
                          .type = titan::OrderType::Limit},
                      trades);

    ASSERT_EQ(trades.size(), 2);

    ASSERT_EQ(trades[0].maker_order_id, 2);
    ASSERT_EQ(trades[0].taker_order_id, 3);
    ASSERT_EQ(trades[0].price, 10050);
    ASSERT_EQ(trades[0].quantity, 50);

    ASSERT_EQ(trades[1].maker_order_id, 1);
    ASSERT_EQ(trades[1].taker_order_id, 3);
    ASSERT_EQ(trades[1].price, 10000);
    ASSERT_EQ(trades[1].quantity, 30);

    // Remaining in book: Order #1 with 70 shares
    ASSERT_EQ(book.total_orders(), 1);
    ASSERT_TRUE(book.has_order(1));
    ASSERT_FALSE(book.has_order(2));
    ASSERT_FALSE(book.has_order(3));

    auto ord1 = book.get_order(1);
    ASSERT_TRUE(ord1.has_value());
    ASSERT_EQ(ord1->remaining_quantity, 70);
    ASSERT_EQ(book.best_bid().value(), 10000);
}

void test_time_priority_fifo()
{
    std::cout << "[TEST] Running test_time_priority_fifo...\n";
    titan::StlOrderBook book;
    std::vector<titan::Trade> trades;

    // Two sell orders at the exact same price (10200)
    // Order A arrives first
    book.submit_order(titan::Order{
                          .order_id = 101,
                          .price = 10200,
                          .initial_quantity = 50,
                          .remaining_quantity = 50,
                          .timestamp = 1000,
                          .side = titan::Side::Sell,
                          .type = titan::OrderType::Limit},
                      trades);

    // Order B arrives second
    book.submit_order(titan::Order{
                          .order_id = 102,
                          .price = 10200,
                          .initial_quantity = 50,
                          .remaining_quantity = 50,
                          .timestamp = 2000,
                          .side = titan::Side::Sell,
                          .type = titan::OrderType::Limit},
                      trades);

    ASSERT_EQ(book.volume_at_price(titan::Side::Sell, 10200), 100);

    // Incoming Buy for 60 shares @ 10200
    book.submit_order(titan::Order{
                          .order_id = 103,
                          .price = 10200,
                          .initial_quantity = 60,
                          .remaining_quantity = 60,
                          .timestamp = 3000,
                          .side = titan::Side::Buy,
                          .type = titan::OrderType::Limit},
                      trades);

    // FIFO requirement: Order 101 must be filled completely first, then 10 shares of Order 102
    ASSERT_EQ(trades.size(), 2);
    ASSERT_EQ(trades[0].maker_order_id, 101);
    ASSERT_EQ(trades[0].quantity, 50);

    ASSERT_EQ(trades[1].maker_order_id, 102);
    ASSERT_EQ(trades[1].quantity, 10);

    // Order 102 must have 40 shares remaining
    ASSERT_FALSE(book.has_order(101));
    ASSERT_TRUE(book.has_order(102));
    ASSERT_EQ(book.get_order(102)->remaining_quantity, 40);
    ASSERT_EQ(book.volume_at_price(titan::Side::Sell, 10200), 40);
}

void test_cancellation()
{
    std::cout << "[TEST] Running test_cancellation...\n";
    titan::StlOrderBook book;
    std::vector<titan::Trade> trades;

    book.submit_order(titan::Order{
                          .order_id = 501,
                          .price = 9900,
                          .initial_quantity = 100,
                          .remaining_quantity = 100,
                          .timestamp = 1000,
                          .side = titan::Side::Buy,
                          .type = titan::OrderType::Limit},
                      trades);

    book.submit_order(titan::Order{
                          .order_id = 502,
                          .price = 9900,
                          .initial_quantity = 200,
                          .remaining_quantity = 200,
                          .timestamp = 1100,
                          .side = titan::Side::Buy,
                          .type = titan::OrderType::Limit},
                      trades);

    ASSERT_EQ(book.total_orders(), 2);
    ASSERT_EQ(book.volume_at_price(titan::Side::Buy, 9900), 300);

    // Cancel order 501
    ASSERT_TRUE(book.cancel_order(501));
    ASSERT_FALSE(book.has_order(501));
    ASSERT_EQ(book.total_orders(), 1);
    ASSERT_EQ(book.volume_at_price(titan::Side::Buy, 9900), 200);

    // Cannot cancel again
    ASSERT_FALSE(book.cancel_order(501));

    // Cancel order 502 (empties the price level)
    ASSERT_TRUE(book.cancel_order(502));
    ASSERT_EQ(book.total_orders(), 0);
    ASSERT_EQ(book.bid_levels(), 0);
    ASSERT_FALSE(book.best_bid().has_value());
}

void test_market_order()
{
    std::cout << "[TEST] Running test_market_order...\n";
    titan::StlOrderBook book;
    std::vector<titan::Trade> trades;

    // Place Sell liquidity: 100 @ 10100, 100 @ 10200
    book.submit_order(titan::Order{
                          .order_id = 1,
                          .price = 10100,
                          .initial_quantity = 100,
                          .remaining_quantity = 100,
                          .timestamp = 1000,
                          .side = titan::Side::Sell,
                          .type = titan::OrderType::Limit},
                      trades);

    book.submit_order(titan::Order{
                          .order_id = 2,
                          .price = 10200,
                          .initial_quantity = 100,
                          .remaining_quantity = 100,
                          .timestamp = 1100,
                          .side = titan::Side::Sell,
                          .type = titan::OrderType::Limit},
                      trades);

    // Market Buy for 150 shares (price is ignored for market orders)
    trades.clear();
    book.submit_order(titan::Order{
                          .order_id = 3,
                          .price = 0,
                          .initial_quantity = 150,
                          .remaining_quantity = 150,
                          .timestamp = 2000,
                          .side = titan::Side::Buy,
                          .type = titan::OrderType::Market},
                      trades);

    ASSERT_EQ(trades.size(), 2);
    ASSERT_EQ(trades[0].maker_order_id, 1);
    ASSERT_EQ(trades[0].quantity, 100);
    ASSERT_EQ(trades[0].price, 10100);

    ASSERT_EQ(trades[1].maker_order_id, 2);
    ASSERT_EQ(trades[1].quantity, 50);
    ASSERT_EQ(trades[1].price, 10200);

    // Remaining in book: Order #2 with 50 shares
    ASSERT_EQ(book.total_orders(), 1);
    ASSERT_EQ(book.get_order(2)->remaining_quantity, 50);

    // Market order 3 should NOT rest in the book
    ASSERT_FALSE(book.has_order(3));
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << " Running StlOrderBook Correctness Tests " << std::endl;
    std::cout << "========================================" << std::endl;

    test_basic_resting_orders();
    test_single_full_match();
    test_maker_price_priority_and_partial_fills();
    test_time_priority_fifo();
    test_cancellation();
    test_market_order();

    std::cout << "========================================" << std::endl;
    std::cout << " ALL UNIT TESTS PASSED SUCCESSFULLY!    " << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
