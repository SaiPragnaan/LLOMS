#include "ingress_message.hpp"
#include "pool_order_book.hpp"
#include "spsc_queue.hpp"
#include "workload.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

void run_spsc_benchmark(size_t order_count, size_t batch_size) {
    std::cout << "\n-----------------------------------------------------------------------------------------\n";
    std::cout << " SPSC Ingress Queue + Matching Core Benchmark (" << order_count 
              << " orders, batch_size=" << batch_size << ")\n";
    std::cout << "-----------------------------------------------------------------------------------------\n";

    auto actions = titan::WorkloadGenerator::generate(titan::WorkloadType::Balanced, order_count, 42);

    titan::PoolOrderBook order_book(order_count);
    titan::SpscQueue<titan::IngressMessage> queue(65536);

    std::atomic<bool> producer_done{false};
    std::vector<uint64_t> latencies_ns;
    latencies_ns.reserve(order_count);

    auto t_start = std::chrono::steady_clock::now();

    // 1. Consumer (Dedicated Matching Thread)
    std::thread matching_thread([&]() {
        std::vector<titan::IngressMessage> batch;
        batch.reserve(batch_size);
        std::vector<titan::Trade> trades;
        trades.reserve(64);

        while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
            size_t n = queue.pop_batch(batch, batch_size);
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }

            auto now = std::chrono::steady_clock::now();
            for (size_t i = 0; i < n; ++i) {
                const auto& msg = batch[i];
                if (msg.command.type == titan::CommandType::New) {
                    order_book.submit_order(msg.command.order, trades);
                } else if (msg.command.type == titan::CommandType::Cancel) {
                    order_book.cancel_order(msg.command.cancel_id);
                }

                auto now_ns = static_cast<titan::Timestamp>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count()
                );
                if (now_ns >= msg.ingress_timestamp) {
                    latencies_ns.push_back(now_ns - msg.ingress_timestamp);
                }
                trades.clear();
            }
            batch.clear();
        }
    });

    // 2. Producer (Network Ingress Thread)
    std::thread ingress_thread([&]() {
        for (size_t i = 0; i < actions.size(); ++i) {
            const auto& act = actions[i];
            auto now_ns = static_cast<titan::Timestamp>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count()
            );

            titan::ParsedCommand cmd;
            if (act.type == titan::ActionType::NewOrder) {
                cmd.type = titan::CommandType::New;
                cmd.order = act.order;
            } else {
                cmd.type = titan::CommandType::Cancel;
                cmd.cancel_id = act.cancel_id;
            }

            titan::IngressMessage msg{
                .sequence = i + 1,
                .client_fd = 1,
                .ingress_timestamp = now_ns,
                .command = cmd
            };

            while (!queue.push(std::move(msg))) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    ingress_thread.join();
    matching_thread.join();

    auto t_end = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    std::sort(latencies_ns.begin(), latencies_ns.end());
    size_t n = latencies_ns.size();

    double throughput = (static_cast<double>(order_count) / (total_ms / 1000.0)) / 1e6;
    double p50 = n > 0 ? latencies_ns[static_cast<size_t>(n * 0.50)] : 0;
    double p95 = n > 0 ? latencies_ns[static_cast<size_t>(n * 0.95)] : 0;
    double p99 = n > 0 ? latencies_ns[static_cast<size_t>(n * 0.99)] : 0;
    double max_lat = n > 0 ? latencies_ns.back() / 1e3 : 0;

    std::cout << "Throughput:     " << std::fixed << std::setprecision(2) << throughput << " M ops/s\n";
    std::cout << "p50 Latency:    " << p50 << " ns\n";
    std::cout << "p95 Latency:    " << p95 << " ns\n";
    std::cout << "p99 Latency:    " << p99 << " ns\n";
    std::cout << "Max Latency:    " << max_lat << " us\n";
    std::cout << "Remaining Book: " << order_book.total_orders() << " orders\n";
}

int main(int argc, char** argv) {
    size_t order_count = 200000;
    if (argc > 1) {
        order_count = std::stoull(argv[1]);
    }

    std::cout << "=========================================================================================\n";
    std::cout << "            TITANMATCH: PHASE 2 SPSC CONCURRENT INGRESS BENCHMARKS                       \n";
    std::cout << "=========================================================================================\n";

    // Compare different batch drain sizes
    run_spsc_benchmark(order_count, 1);    // Single pop()
    run_spsc_benchmark(order_count, 16);   // Small batch
    run_spsc_benchmark(order_count, 64);   // Optimized batch
    run_spsc_benchmark(order_count, 128);  // Large batch

    std::cout << "\n=========================================================================================\n";
    std::cout << " PHASE 2 BENCHMARKS COMPLETED SUCCESSFULLY!                                              \n";
    std::cout << "=========================================================================================\n";
    return 0;
}
