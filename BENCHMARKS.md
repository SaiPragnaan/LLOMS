**Engine A:**  STL Baseline Engine -- this uses map with STL based doubly linked list, with each 
                order entry having another unordered map for its orderId vs its memory location.

**Engine B:**  Contiguous Pool Order Book Engine -- this uses a contiguos pool of memory slots for orders
                along with a map, where each map entry has a 

# TITANMATCH: BENCHMARK SUITE (Engine A vs B COMPARISON)

## Benchmark Results (200,000 Operations per Workload)

### 1. Insert-Heavy Workload (80% New Orders, 10% Cancels, 10% Crosses)
| Metric | Engine A (STL) | Engine B (Pool + Intrusive Free-List) | Improvement |
| :--- | :---: | :---: | :---: |
| **Throughput** | 7.01 M ops/s | **9.92 M ops/s** | **+41%** |
| **Mean Latency** | 126.46 ns | **82.26 ns** | **+35%** |
| **p50 Latency (median)** | 103.00 ns | **74.00 ns** | **+28%** |
| **p95 Latency** | 221.00 ns | **153.00 ns** | **+31%** |
| **p99 Latency (tail)** | 1007.00 ns | **303.00 ns** | **+70%** |
| **p99.9 Latency** | 1864.00 ns | **841.00 ns** | **+55%** |
| **Max Latency** | 447.31 μs | **6.81 μs** | **+98%** |

> **Key Takeaway**: By eliminating `malloc`/`free` calls per order and reusing pre-allocated contiguous memory slots, Engine B eliminated the massive 447 μs tail latency spikes down to 6.8 μs and boosted throughput by **41%**.

---

### 2. Balanced Market Workload (50% Buys, 50% Sells, Gaussian Spread)
| Metric | Engine A (STL) | Engine B (Pool + Intrusive Free-List) | Improvement |
| :--- | :---: | :---: | :---: |
| **Throughput** | 7.24 M ops/s | **10.19 M ops/s** | **+41%** |
| **Mean Latency** | 117.58 ns | **79.82 ns** | **+32%** |
| **p50 Latency (median)** | 90.00 ns | **64.00 ns** | **+29%** |
| **p95 Latency** | 243.00 ns | **153.00 ns** | **+37%** |
| **p99 Latency (tail)** | 602.00 ns | **356.00 ns** | **+41%** |
| **p99.9 Latency** | 2275.00 ns | **1479.00 ns** | **+35%** |
| **Max Latency** | 104.84 μs | **12.59 μs** | **+88%** |

---

### 3. Cancel-Heavy Market Workload (200,000 Operations)
| Metric | Engine A (STL) | Engine B (Pool) | Improvement |
| --- | --- | --- | --- |
| **Throughput** | 12.68 M ops/s | **13.80 M ops/s** | **+9%** |
| **Mean Latency** | 62.78 ns | **56.26 ns** | **+10%** |
| **p50 Latency (median)** | 59.00 ns | **51.00 ns** | **+14%** |
| **p95 Latency** | 114.00 ns | **99.00 ns** | **+13%** |
| **p99 Latency (tail)** | 170.00 ns | **146.00 ns** | **+14%** |
| **p99.9 Latency** | 283.00 ns | **232.00 ns** | **+18%** |
| **Max Latency** | **4.51 us** | 8.38 us | **-86%** |

## How to Reproduce

Run the full suite in the terminal:

```bash
make test

make bench
```
