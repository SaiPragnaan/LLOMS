#include "ingress_message.hpp"
#include "pool_order_book.hpp"
#include "spsc_queue.hpp"
#include "workload.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

namespace {

using clock = std::chrono::steady_clock;

struct Completion {
    titan::Timestamp ingress_ts{0};
};

titan::Timestamp now_ns() {
    return static_cast<titan::Timestamp>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count());
}

titan::IngressMessage make_message(size_t index, const titan::OrderAction& act, titan::Timestamp ts) {
    titan::ParsedCommand cmd;
    if (act.type == titan::ActionType::NewOrder) {
        cmd.type = titan::CommandType::New;
        cmd.order = act.order;
    } else {
        cmd.type = titan::CommandType::Cancel;
        cmd.cancel_id = act.cancel_id;
    }
    return titan::IngressMessage{
        .sequence = index + 1,
        .client_fd = 1,
        .ingress_timestamp = ts,
        .command = cmd};
}

void execute_command(titan::PoolOrderBook& book, const titan::IngressMessage& msg, std::vector<titan::Trade>& trades) {
    if (msg.command.type == titan::CommandType::New) {
        book.submit_order(msg.command.order, trades);
    } else if (msg.command.type == titan::CommandType::Cancel) {
        book.cancel_order(msg.command.cancel_id);
    }
    trades.clear();
}

struct LatencyStats {
    size_t samples{0};
    double mean_ns{0};
    double p50_ns{0};
    double p95_ns{0};
    double p99_ns{0};
    double max_ns{0};
};

LatencyStats compute_latency_stats(std::vector<uint64_t>& latencies_ns, size_t warmup) {
    LatencyStats stats;
    if (latencies_ns.size() <= warmup) {
        return stats;
    }

    latencies_ns.erase(latencies_ns.begin(), latencies_ns.begin() + static_cast<std::ptrdiff_t>(warmup));
    std::sort(latencies_ns.begin(), latencies_ns.end());

    const size_t n = latencies_ns.size();
    stats.samples = n;
    stats.p50_ns = static_cast<double>(latencies_ns[n * 50 / 100]);
    stats.p95_ns = static_cast<double>(latencies_ns[n * 95 / 100]);
    stats.p99_ns = static_cast<double>(latencies_ns[n * 99 / 100]);
    stats.max_ns = static_cast<double>(latencies_ns.back());

    const uint64_t sum = std::accumulate(latencies_ns.begin(), latencies_ns.end(), uint64_t{0});
    stats.mean_ns = static_cast<double>(sum) / static_cast<double>(n);
    return stats;
}

void print_latency(const LatencyStats& s) {
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "Samples:        " << s.samples << " (after warmup)\n";
    std::cout << "Mean latency:   " << s.mean_ns << " ns  (" << std::setprecision(2) << (s.mean_ns / 1e3)
              << " us)\n";
    std::cout << std::setprecision(0);
    std::cout << "p50 latency:    " << s.p50_ns << " ns  (" << std::setprecision(2) << (s.p50_ns / 1e3) << " us)\n";
    std::cout << std::setprecision(0);
    std::cout << "p95 latency:    " << s.p95_ns << " ns  (" << std::setprecision(2) << (s.p95_ns / 1e3) << " us)\n";
    std::cout << std::setprecision(0);
    std::cout << "p99 latency:    " << s.p99_ns << " ns  (" << std::setprecision(2) << (s.p99_ns / 1e3) << " us)\n";
    std::cout << std::setprecision(0);
    std::cout << "Max latency:    " << s.max_ns << " ns  (" << std::setprecision(2) << (s.max_ns / 1e3) << " us)\n";
}

size_t drain_completions(titan::SpscQueue<Completion>& completions,
                         std::vector<Completion>& batch,
                         size_t max_items,
                         std::vector<uint64_t>& latencies_ns) {
    batch.clear();
    const size_t n = completions.pop_batch(batch, max_items);
    const titan::Timestamp t1 = now_ns();
    for (size_t i = 0; i < n; ++i) {
        if (t1 >= batch[i].ingress_ts) {
            latencies_ns.push_back(t1 - batch[i].ingress_ts);
        }
    }
    return n;
}

// Saturated ingest: producer never waits for completions. Reports pipeline
// throughput only. Latency is omitted — it would just be "time spent in a full ring".
void run_open_loop_throughput(const std::vector<titan::OrderAction>& actions, size_t batch_size) {
    std::cout << "\n-----------------------------------------------------------------------------------------\n";
    std::cout << " Saturated throughput (open-loop ingest, batch_size=" << batch_size << ", n="
              << actions.size() << ")\n";
    std::cout << "-----------------------------------------------------------------------------------------\n";

    titan::PoolOrderBook order_book(actions.size());
    titan::SpscQueue<titan::IngressMessage> ingress(65536);
    std::atomic<bool> producer_done{false};

    auto t_start = clock::now();

    std::thread matching_thread([&]() {
        std::vector<titan::IngressMessage> batch;
        batch.reserve(batch_size);
        std::vector<titan::Trade> trades;
        trades.reserve(64);

        while (!producer_done.load(std::memory_order_acquire) || !ingress.empty()) {
            const size_t n = ingress.pop_batch(batch, batch_size);
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            for (size_t i = 0; i < n; ++i) {
                execute_command(order_book, batch[i], trades);
            }
            batch.clear();
        }
    });

    std::thread ingress_thread([&]() {
        for (size_t i = 0; i < actions.size(); ++i) {
            titan::IngressMessage msg = make_message(i, actions[i], 0);
            while (!ingress.push(std::move(msg))) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    ingress_thread.join();
    matching_thread.join();

    const double total_s = std::chrono::duration<double>(clock::now() - t_start).count();
    const double throughput_mops = (static_cast<double>(actions.size()) / total_s) / 1e6;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Throughput:     " << throughput_mops << " M ops/s\n";
    std::cout << "Wall time:      " << (total_s * 1e3) << " ms\n";
    std::cout << "Remaining book: " << order_book.total_orders() << " orders\n";
    std::cout << "Note:           latency omitted (queue is kept full on purpose)\n";
}

// Closed-loop E2E: ingest thread may have at most `window` unacked commands.
// Latency = successful ingress push → ingest thread pops the matching completion
// (ingress queue + match + response queue), same two-thread shape as the gateway.
void run_closed_loop_e2e(const std::vector<titan::OrderAction>& actions, size_t batch_size, size_t window) {
    std::cout << "\n-----------------------------------------------------------------------------------------\n";
    std::cout << " Closed-loop E2E (window=" << window << ", batch_size=" << batch_size << ", n="
              << actions.size() << ")\n";
    std::cout << "-----------------------------------------------------------------------------------------\n";

    titan::PoolOrderBook order_book(actions.size());
    titan::SpscQueue<titan::IngressMessage> ingress(65536);
    titan::SpscQueue<Completion> completions(65536);
    std::atomic<bool> producer_done{false};

    const size_t warmup = std::min<size_t>(10000, actions.size() / 10);
    std::vector<uint64_t> latencies_ns;
    latencies_ns.reserve(actions.size());

    auto t_start = clock::now();

    std::thread matching_thread([&]() {
        std::vector<titan::IngressMessage> batch;
        batch.reserve(batch_size);
        std::vector<titan::Trade> trades;
        trades.reserve(64);

        while (!producer_done.load(std::memory_order_acquire) || !ingress.empty()) {
            const size_t n = ingress.pop_batch(batch, batch_size);
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            for (size_t i = 0; i < n; ++i) {
                execute_command(order_book, batch[i], trades);
                Completion c{.ingress_ts = batch[i].ingress_timestamp};
                while (!completions.push(c)) {
                    std::this_thread::yield();
                }
            }
            batch.clear();
        }
    });

    std::thread ingress_thread([&]() {
        std::vector<Completion> ack_batch;
        ack_batch.reserve(64);
        size_t in_flight = 0;
        size_t i = 0;

        auto wait_for_credit = [&]() {
            while (in_flight >= window) {
                const size_t got = drain_completions(completions, ack_batch, 64, latencies_ns);
                if (got == 0) {
                    std::this_thread::yield();
                } else {
                    in_flight -= got;
                }
            }
        };

        for (; i < actions.size(); ++i) {
            wait_for_credit();

            titan::Timestamp ts = now_ns();
            titan::IngressMessage msg = make_message(i, actions[i], ts);
            while (!ingress.push(std::move(msg))) {
                const size_t got = drain_completions(completions, ack_batch, 64, latencies_ns);
                if (got == 0) {
                    std::this_thread::yield();
                } else {
                    in_flight -= got;
                }
                ts = now_ns();
                msg = make_message(i, actions[i], ts);
            }
            ++in_flight;
        }

        while (in_flight > 0) {
            const size_t got = drain_completions(completions, ack_batch, 64, latencies_ns);
            if (got == 0) {
                std::this_thread::yield();
            } else {
                in_flight -= got;
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    ingress_thread.join();
    matching_thread.join();

    const double total_s = std::chrono::duration<double>(clock::now() - t_start).count();
    const double throughput_mops = (static_cast<double>(actions.size()) / total_s) / 1e6;
    const LatencyStats stats = compute_latency_stats(latencies_ns, warmup);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Throughput:     " << throughput_mops << " M ops/s\n";
    std::cout << "Wall time:      " << (total_s * 1e3) << " ms\n";
    print_latency(stats);
    std::cout << "Remaining book: " << order_book.total_orders() << " orders\n";
}

} // namespace

int main(int argc, char** argv) {
    size_t order_count = 200000;
    if (argc > 1) {
        order_count = std::stoull(argv[1]);
    }

    const auto actions = titan::WorkloadGenerator::generate(titan::WorkloadType::Balanced, order_count, 42);

    std::cout << "=========================================================================================\n";
    std::cout << "            TITANMATCH: SPSC PIPELINE BENCHMARKS                                         \n";
    std::cout << "=========================================================================================\n";
    std::cout << "\nWhat is measured (in-process, no TCP):\n";
    std::cout << "  Throughput (open-loop): commands matched / wall time with a sprinting producer.\n";
    std::cout << "  E2E latency (closed-loop): ingress push -> ingest thread reads the completion\n";
    std::cout << "    (ingress SPSC + matching + response SPSC). In-flight is capped by `window` so\n";
    std::cout << "    percentiles are not dominated by a 65k-deep saturated ring.\n";

    std::cout << "\n========== PART 1: Max pipeline throughput (latency not reported) ==========\n";
    for (size_t batch : {size_t{1}, size_t{16}, size_t{64}, size_t{128}}) {
        run_open_loop_throughput(actions, batch);
    }

    std::cout << "\n========== PART 2: Closed-loop E2E vs outstanding window (batch=64) ==========\n";
    for (size_t window : {size_t{1}, size_t{16}, size_t{64}, size_t{256}, size_t{1024}, size_t{4096}}) {
        run_closed_loop_e2e(actions, 64, window);
    }

    std::cout << "\n========== PART 3: Fair batch comparison at the same window (256) ==========\n";
    for (size_t batch : {size_t{1}, size_t{16}, size_t{64}, size_t{128}}) {
        run_closed_loop_e2e(actions, batch, 256);
    }

    std::cout << "\n=========================================================================================\n";
    std::cout << " BENCHMARKS COMPLETED                                                                    \n";
    std::cout << "=========================================================================================\n";
    return 0;
}
