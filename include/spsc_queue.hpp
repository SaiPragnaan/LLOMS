#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

namespace titan {

// Hardware destructive interference size for cache-line alignment (usually 64 bytes on x86_64)
#if defined(__cpp_lib_hardware_interference_size)
    using std::hardware_destructive_interference_size;
#else
    constexpr size_t hardware_destructive_interference_size = 64;
#endif

template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(size_t capacity = 65536)
        : capacity_(round_up_power_of_two(capacity)),
          mask_(capacity_ - 1),
          buffer_(capacity_) {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    ~SpscQueue() = default;

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;

    // Called exclusively by PRODUCER (Network Thread)
    bool push(const T& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t current_head = head_.load(std::memory_order_acquire);

        // If queue is full, return false (backpressure)
        if (current_tail - current_head >= capacity_) {
            return false;
        }

        buffer_[current_tail & mask_] = item;
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    // Move-push for zero-copy
    bool push(T&& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t current_head = head_.load(std::memory_order_acquire);

        if (current_tail - current_head >= capacity_) {
            return false;
        }

        buffer_[current_tail & mask_] = std::move(item);
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    // Called exclusively by CONSUMER (Matching Thread)
    bool pop(T& out_item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_acquire);

        if (current_head == current_tail) {
            return false; // Queue empty
        }

        out_item = std::move(buffer_[current_head & mask_]);
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    // Batch pop: drains up to max_items in one pass for amortized memory synchronization
    size_t pop_batch(std::vector<T>& out_items, size_t max_items) {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_acquire);

        const size_t available = current_tail - current_head;
        if (available == 0) {
            return 0;
        }

        const size_t count = (available < max_items) ? available : max_items;
        out_items.reserve(out_items.size() + count);

        for (size_t i = 0; i < count; ++i) {
            out_items.push_back(std::move(buffer_[(current_head + i) & mask_]));
        }

        head_.store(current_head + count, std::memory_order_release);
        return count;
    }

    [[nodiscard]] size_t size() const noexcept {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (tail >= head) ? (tail - head) : 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] size_t capacity() const noexcept {
        return capacity_;
    }

private:
    static size_t round_up_power_of_two(size_t val) noexcept {
        size_t power = 1;
        while (power < val) {
            power <<= 1;
        }
        return power;
    }

    const size_t capacity_;
    const size_t mask_;
    std::vector<T> buffer_;

    // Padded to separate cache lines to eliminate false sharing between producer and consumer
    alignas(hardware_destructive_interference_size) std::atomic<size_t> tail_{0};
    alignas(hardware_destructive_interference_size) std::atomic<size_t> head_{0};
};

} // namespace titan
