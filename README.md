# LLOMS — Low-Latency Order Management System

A high-performance C++20 limit-order matching engine designed around efficient order-book management, low-latency cancellation, and concurrent order ingestion.

### Highlights

* **Price-time priority** matching with support for partial fills and deterministic execution.
* **Contiguous order pool** with index-based links and O(1)-average order cancellation.
* **Lock-free SPSC ingress queue** separating concurrent order ingestion from the matching core.
* **Batch-based queue consumption** with benchmarking across different batch sizes.
* Deterministic benchmarks covering **insert, match, and cancel-heavy workloads**, measuring throughput and p50/p95/p99 latency.

### Performance

On the benchmark workload:

* **41% higher throughput** with the contiguous order-pool design.
* **70% lower p99 latency** and **98% lower maximum latency** versus the STL baseline.
* **12.37M ops/sec** achieved with SPSC ingress and batch-64 consumption.

The engine is intentionally designed with a **single-threaded matching core** to preserve strict ordering, while concurrency is introduced at the ingestion layer.

More detailedly mentioned in the `BENCHMARKS.md`