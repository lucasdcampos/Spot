#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>

namespace Spot::Detail {

// A fixed-capacity queue between one thread that pushes and one that pops, without locks or allocations, as the
// game sends the audio thread its commands. Several threads may push only while holding a lock of their own.
template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "The capacity is a power of two");

public:
    // False when the queue is full.
    bool Push(const T& value) {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail - m_head.load(std::memory_order_acquire) == Capacity) {
            return false;
        }
        m_items[tail & (Capacity - 1)] = value;
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> Pop() {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        if (head == m_tail.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        T value = m_items[head & (Capacity - 1)];
        m_head.store(head + 1, std::memory_order_release);
        return value;
    }

    bool IsEmpty() const { return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire); }

private:
    std::array<T, Capacity> m_items{};
    // Apart, so the two threads do not share a cache line; padded rather than aligned, which compilers warn about.
    std::atomic<std::size_t> m_head = 0;
    std::array<std::byte, 64 - sizeof(std::atomic<std::size_t>)> m_padding{};
    std::atomic<std::size_t> m_tail = 0;
};

} // namespace Spot::Detail
