#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <utility>

namespace gitar {

// Lock-free single-producer/single-consumer queue. Real-time safe: no
// allocation, no locks, no exceptions.
template <typename T, std::size_t Capacity>
class SpscRingBuffer {
    static_assert(Capacity > 0, "Capacity must be greater than zero");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

   public:
    SpscRingBuffer() = default;
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    bool push(const T& value) {
        return push_impl(value);
    }

    bool push(T&& value) {
        return push_impl(std::move(value));
    }

    bool pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        // Acquire pairs with the producer's release store, making the slot
        // contents visible before we read them.
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        out = std::move(buffer_[tail & (Capacity - 1)]);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return size() == 0;
    }

    bool full() const {
        return size() == Capacity;
    }

    std::size_t size() const {
        // Unsigned wraparound of the monotonic counters is intentional; the
        // difference is the number of queued items.
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

    static constexpr std::size_t capacity() {
        return Capacity;
    }

   private:
    template <typename U>
    bool push_impl(U&& value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        // Acquire pairs with the consumer's release store, so the slot we are
        // about to overwrite is guaranteed to be free.
        if (head - tail_.load(std::memory_order_acquire) >= Capacity) {
            return false;
        }
        buffer_[head & (Capacity - 1)] = std::forward<U>(value);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    std::array<T, Capacity> buffer_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

}  // namespace gitar
