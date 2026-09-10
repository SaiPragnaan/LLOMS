#include "spsc_queue.hpp"

#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion FAILED at " << __FILE__ << ":" << __LINE__ << " -> " << #cond << "\n"; \
            std::exit(1); \
        } \
    } while (0)

#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))

void test_basic_spsc() {
    std::cout << "[TEST] Running test_basic_spsc...\n";
    titan::SpscQueue<int> q(4); // rounds up to 4
    ASSERT_EQ(q.capacity(), 4);
    ASSERT_TRUE(q.empty());
    ASSERT_EQ(q.size(), 0);

    ASSERT_TRUE(q.push(10));
    ASSERT_TRUE(q.push(20));
    ASSERT_TRUE(q.push(30));
    ASSERT_TRUE(q.push(40));
    // Now full
    ASSERT_TRUE(!q.push(50));
    ASSERT_EQ(q.size(), 4);

    int val = 0;
    ASSERT_TRUE(q.pop(val));
    ASSERT_EQ(val, 10);
    ASSERT_EQ(q.size(), 3);

    ASSERT_TRUE(q.push(50));
    ASSERT_EQ(q.size(), 4);

    std::vector<int> batch;
    size_t popped = q.pop_batch(batch, 10);
    ASSERT_EQ(popped, 4);
    ASSERT_EQ(batch.size(), 4);
    ASSERT_EQ(batch[0], 20);
    ASSERT_EQ(batch[1], 30);
    ASSERT_EQ(batch[2], 40);
    ASSERT_EQ(batch[3], 50);
    ASSERT_TRUE(q.empty());
}

void test_concurrent_producer_consumer(size_t count) {
    std::cout << "[TEST] Running test_concurrent_producer_consumer (" << count << " items)...\n";
    titan::SpscQueue<uint64_t> q(65536);

    std::thread producer([&]() {
        for (uint64_t i = 0; i < count; ++i) {
            while (!q.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::vector<uint64_t> consumed;
    consumed.reserve(count);

    std::thread consumer([&]() {
        std::vector<uint64_t> batch;
        batch.reserve(128);

        while (consumed.size() < count) {
            size_t n = q.pop_batch(batch, 128);
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            for (size_t i = 0; i < n; ++i) {
                consumed.push_back(batch[i]);
            }
            batch.clear();
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(consumed.size(), count);
    for (uint64_t i = 0; i < count; ++i) {
        ASSERT_EQ(consumed[i], i);
    }
    std::cout << "[PASS] Successfully verified " << count << " sequentially ordered items with zero drops.\n";
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "        RUNNING LOCK-FREE SPSC QUEUE TESTS              \n";
    std::cout << "=========================================================\n";

    test_basic_spsc();
    test_concurrent_producer_consumer(1000000);

    std::cout << "=========================================================\n";
    std::cout << " ALL SPSC QUEUE TESTS PASSED SUCCESSFULLY!               \n";
    std::cout << "=========================================================\n";
    return 0;
}
