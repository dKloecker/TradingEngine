//
// Created by Dominic Kloecker on 31/03/2026.
//
#ifndef TRADING_SPSC_RING_BUFFER_H
#define TRADING_SPSC_RING_BUFFER_H
#include <array>
#include <atomic>
#include <optional>
#include <new>
#include "util.h"

namespace dsl {
template<typename T, size_t Capacity>
    requires PowerOfTwo<Capacity>
class spsc_queue_imp {
    using value_type                                     = T;
    static constexpr size_t         ELEMENT_SIZE         = sizeof(T);
    static constexpr size_t         BUFFER_SIZE          = Capacity * ELEMENT_SIZE;
    std::byte                       buffer_[BUFFER_SIZE] = {};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};

    /** Maps an unbounded index to a buffer position via bitmask */
    static constexpr size_t slot(const size_t index) {
        return index & (Capacity - 1);
    }

    /** Returns a laundered pointer to prevent compiler optimization invalidating pointers */
    constexpr T *at(const size_t slot) {
        return std::launder(reinterpret_cast<T *>(buffer_) + slot);
    }

public:
    bool empty() const {
        return (head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_acquire));
    }

    /** @return true on success, false if the queue is full */
    bool push(const T &data) {
        const size_t curr = tail_.load(std::memory_order_relaxed);
        if (curr - head_.load(std::memory_order_acquire) >= Capacity) return false;
        new(at(slot(curr))) T(data);
        tail_.store(curr + 1, std::memory_order_release);
        return true;
    }

    /**
     * Moves the front element into the fill parameter and destroys it
     * @return true on success, false if the queue is empty.
     */
    bool pop(T &fill) {
        const size_t curr = head_.load(std::memory_order_relaxed);
        if (tail_.load(std::memory_order_acquire) - curr == 0) return false;

        T *ptr = at(slot(curr));
        fill   = std::move(*ptr);
        ptr->~T();

        head_.store(curr + 1, std::memory_order_release);
        return true;
    }

    /**
     * @return The moved front element, or std::nullopt if the queue is empty.
     */
    std::optional<T> try_pop() {
        const size_t curr = head_.load(std::memory_order_relaxed);
        if (tail_.load(std::memory_order_acquire) - curr == 0) return std::nullopt;

        T *              ptr    = at(slot(curr));
        std::optional<T> result = std::move(*ptr);
        ptr->~T();

        head_.store(curr + 1, std::memory_order_release);
        return result;
    }

    /**
     * @return Pointer to the front element, or nullptr if empty.
     * @warning will be invalidated once the element is popped.
     */
    const T *top() {
        if (empty()) return nullptr;
        return at(slot(head_.load(std::memory_order_relaxed)));
    }

    void reset() {
        // Clear queue and delete any remaining elements from it
        while (slot(head_) < slot(tail_)) at(slot(head_++))->~T();
    }

    ~spsc_queue_imp() {
        reset();
    }
};

/**
 * @brief Lock-free single-producer single-consumer bounded queue, with a capacity at minimum
 * the requested size.
 *
 * @tparam T                  Element type
 * @tparam RequestedCapacity  Minimum number of elements the queue can hold

 */
template<typename T, size_t RequestedCapacity>
class spsc_queue : public spsc_queue_imp<T, round_up_pow2(RequestedCapacity)> {
public:
    static constexpr size_t capacity = round_up_pow2(RequestedCapacity);
};
}


#endif //TRADING_SPSC_RING_BUFFER_H
