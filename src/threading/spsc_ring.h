/*
    NINJAM CLAP Plugin - spsc_ring.h
    Lock-free Single-Producer Single-Consumer ring buffer
    
    Copyright (C) 2024 NINJAM CLAP Contributors
    Licensed under GPLv2+
*/

#ifndef SPSC_RING_H
#define SPSC_RING_H

#include <atomic>
#include <array>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace jamwide {

/**
 * Lock-free SPSC (Single-Producer Single-Consumer) ring buffer.
 * 
 * Thread Safety:
 *   - One thread may call try_push() (producer)
 *   - One thread may call try_pop()/drain() (consumer)
 *   - Different threads for producer and consumer is safe
 * 
 * @tparam T      Element type (must be trivially copyable or movable)
 * @tparam N      Capacity (must be power of 2 for efficient masking)
 */
template <typename T, std::size_t N>
class SpscRing {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
    static_assert(N > 0, "N must be greater than 0");

public:
    SpscRing() : head_(0), tail_(0) {}

    // Non-copyable, non-movable
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;
    SpscRing(SpscRing&&) = delete;
    SpscRing& operator=(SpscRing&&) = delete;

    /**
     * Try to push an element (producer only).
     * @param value The value to push
     * @return true if pushed, false if queue is full
     */
    bool try_push(const T& value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next_head = (head + 1) & mask_;
        
        // Check if full (next head would catch up to tail)
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false;  // Full
        }
        
        buffer_[head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    /**
     * Try to push an element by move (producer only).
     * @param value The value to move-push
     * @return true if pushed, false if queue is full
     */
    bool try_push(T&& value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next_head = (head + 1) & mask_;
        
        if (next_head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        
        buffer_[head] = std::move(value);
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    /**
     * Try to pop an element (consumer only).
     * @return The popped value, or std::nullopt if queue is empty
     */
    std::optional<T> try_pop() {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        
        // Check if empty (tail equals head)
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;  // Empty
        }
        
        T value = std::move(buffer_[tail]);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return value;
    }

    /**
     * Drain all available elements (consumer only).
     * Calls the provided callback for each element.
     * 
     * @tparam Func Callable with signature void(T&&) or void(const T&)
     * @param func Callback to invoke for each element
     * @return Number of elements drained
     */
    template <typename Func>
    std::size_t drain(Func&& func) {
        std::size_t count = 0;
        while (auto value = try_pop()) {
            func(std::move(*value));
            ++count;
        }
        return count;
    }

    /**
     * Check if queue is empty.
     */
    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    /**
     * Get current size.
     */
    std::size_t size() const {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & mask_;
    }

    /**
     * Get capacity.
     */
    static constexpr std::size_t capacity() { return N; }

private:
    static constexpr std::size_t mask_ = N - 1;
    
    std::array<T, N> buffer_;
    
    // Separate cache lines to avoid false sharing
    alignas(64) std::atomic<std::size_t> head_;
    alignas(64) std::atomic<std::size_t> tail_;
};

} // namespace jamwide

#endif // SPSC_RING_H
