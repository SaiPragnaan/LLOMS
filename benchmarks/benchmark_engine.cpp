#include "pool_order_book.hpp"
#include "stl_order_book.hpp"
#include "workload.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

template <typename Engine>
titan::BenchmarkStats run_benchmark(
    Engine &engine,
    const std::vector<titan::OrderAction> &actions,
    size_t warmup_count = 10000)
{
    std::vector<titan::Trade> trades;
    trades.reserve(actions.size());

    // 1. Warmup
    for (size_t i = 0; i < std::min(warmup_count, actions.size()); ++i)
    {
        if (actions[i].type == titan::ActionType::NewOrder)
        {
            engine.submit_order(actions[i].order, trades);
        }
        else
        {
            engine.cancel_order(actions[i].cancel_id);
        }
    }
    engine.clear();
    trades.clear();

    // 2. Timed Benchmark Run
    std::vector<uint64_t> latencies_ns;
    latencies_ns.reserve(actions.size());

    auto t_start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < actions.size(); ++i)
    {
        const auto &act = actions[i];

        auto op_start = std::chrono::steady_clock::now();
        if (act.type == titan::ActionType::NewOrder)
        {
            engine.submit_order(act.order, trades);
        }
        else
        {
            engine.cancel_order(act.cancel_id);
        }
        auto op_end = std::chrono::steady_clock::now();

        uint64_t dur = std::chrono::duration_cast<std::chrono::nanoseconds>(op_end - op_start).count();
        latencies_ns.push_back(dur);
    }

    auto t_end = std::chrono::steady_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    return titan::calculate_stats(latencies_ns, total_ms, trades.size(), engine.total_orders());
}

void print_comparison(const std::string &workload_name, size_t count, const titan::BenchmarkStats &stl, const titan::BenchmarkStats &pool)
{
    std::cout << "\n=========================================================================================\n";
    std::cout << " WORKLOAD: " << workload_name << " (" << count << " Operations)\n";
    std::cout << "=========================================================================================\n";
    std::cout << std::left << std::setw(26) << "Metric"
              << std::right << std::setw(18) << "Engine A (STL)"
              << std::setw(18) << "Engine B (Pool)"
              << std::setw(20) << "Improvement" << "\n";
    std::cout << "-----------------------------------------------------------------------------------------\n";

    auto print_row = [](const std::string &label, double a, double b, const std::string &unit, bool higher_is_better)
    {
        double diff_pct = higher_is_better ? ((b - a) / a * 100.0) : ((a - b) / a * 100.0);
        std::string sign = diff_pct >= 0 ? "+" : "";
        std::cout << std::left << std::setw(26) << label
                  << std::right << std::setw(14) << std::fixed << std::setprecision(2) << a << " " << unit
                  << std::setw(14) << std::fixed << std::setprecision(2) << b << " " << unit
                  << std::setw(18) << (sign + std::to_string(static_cast<int>(std::round(diff_pct))) + "%") << "\n";
    };

    print_row("Throughput", stl.ops_per_sec / 1e6, pool.ops_per_sec / 1e6, "M ops/s", true);
    print_row("Mean Latency", stl.mean_ns, pool.mean_ns, "ns", false);
    print_row("p50 Latency (median)", stl.p50_ns, pool.p50_ns, "ns", false);
    print_row("p95 Latency", stl.p95_ns, pool.p95_ns, "ns", false);
    print_row("p99 Latency (tail)", stl.p99_ns, pool.p99_ns, "ns", false);
    print_row("p99.9 Latency", stl.p99_9_ns, pool.p99_9_ns, "ns", false);
    print_row("Max Latency", stl.max_ns / 1e3, pool.max_ns / 1e3, "us", false);
    std::cout << "-----------------------------------------------------------------------------------------\n";
    std::cout << "Trades Matched: " << pool.total_trades << " | Remaining Orders: " << pool.total_orders_in_book << "\n";
}

int main(int argc, char **argv)
{
    size_t order_count = 200000;
    if (argc > 1)
    {
        order_count = std::stoull(argv[1]);
    }

    std::cout << "\n=========================================================================================\n";
    std::cout << "                   TITANMATCH: BENCHMARK SUITE (A vs B COMPARISON)                       \n";
    std::cout << "=========================================================================================\n";

    // 1. Balanced Market Workload
    {
        auto actions = titan::WorkloadGenerator::generate(titan::WorkloadType::Balanced, order_count, 42);
        titan::StlOrderBook stl_book;
        titan::PoolOrderBook pool_book(order_count);

        auto stl_stats = run_benchmark(stl_book, actions);
        auto pool_stats = run_benchmark(pool_book, actions);

        print_comparison("Balanced Market", order_count, stl_stats, pool_stats);
    }

    // 2. Insert-Heavy Workload (80% inserts, 10% cancels, 10% crosses)
    {
        auto actions = titan::WorkloadGenerator::generate(titan::WorkloadType::InsertHeavy, order_count, 100);
        titan::StlOrderBook stl_book;
        titan::PoolOrderBook pool_book(order_count);

        auto stl_stats = run_benchmark(stl_book, actions);
        auto pool_stats = run_benchmark(pool_book, actions);

        print_comparison("Insert-Heavy Market", order_count, stl_stats, pool_stats);
    }

    // 3. Cancel-Heavy Workload (40% inserts, 50% cancels, 10% crosses)
    {
        auto actions = titan::WorkloadGenerator::generate(titan::WorkloadType::CancelHeavy, order_count, 999);
        titan::StlOrderBook stl_book;
        titan::PoolOrderBook pool_book(order_count);

        auto stl_stats = run_benchmark(stl_book, actions);
        auto pool_stats = run_benchmark(pool_book, actions);

        print_comparison("Cancel-Heavy Market", order_count, stl_stats, pool_stats);
    }

    std::cout << "\n=========================================================================================\n";
    std::cout << " BENCHMARKS COMPLETED SUCCESSFULLY!                                                      \n";
    std::cout << "=========================================================================================\n";
    return 0;
}
