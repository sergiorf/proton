// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace proton::core {

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // padded due to alignas: intended, keeps the indices apart
#endif

// Lock-free queue between exactly one producer thread and one consumer thread
// (ARCHITECTURE.md section 4). Only the producer may call tryPush and only the consumer tryPop;
// capacity() may be called from either.
//
// The capacity is fixed at construction and rounded up to a power of two. Elements only need to
// be move-constructible and move-assignable, so move-only types such as SimChunk fit. Publishing
// uses release stores and observing uses acquire loads, so everything the producer wrote into an
// element is visible to the consumer that pops it.
template <typename T>
class SpscRing
{
public:
    // Holds at least `capacity` elements (a capacity of 0 is treated as 1).
    explicit SpscRing(std::size_t capacity)
        : slots_(std::bit_ceil(capacity == 0 ? std::size_t{1} : capacity)),
          mask_(slots_.size() - 1)
    {
    }

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    std::size_t capacity() const noexcept { return slots_.size(); }

    // Producer only. Moves `value` in and returns true, or returns false when the ring is full and
    // leaves `value` untouched so the caller can retry it.
    bool tryPush(T&& value)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail - cachedHead_ == slots_.size()) {
            cachedHead_ = head_.load(std::memory_order_acquire);
            if (tail - cachedHead_ == slots_.size()) {
                return false;
            }
        }
        slots_[tail & mask_] = std::move(value);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer only. Returns the oldest element, or nothing when the ring is empty.
    std::optional<T> tryPop()
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == cachedTail_) {
            cachedTail_ = tail_.load(std::memory_order_acquire);
            if (head == cachedTail_) {
                return std::nullopt;
            }
        }
        std::optional<T>& slot = slots_[head & mask_];
        std::optional<T> value = std::move(slot);
        slot.reset(); // release the element's resources now, not when the slot is reused
        head_.store(head + 1, std::memory_order_release);
        return value;
    }

private:
    static constexpr std::size_t kCacheLine = 64;

    std::vector<std::optional<T>> slots_;
    const std::size_t mask_;

    // Indices count up forever; unsigned wrap-around keeps `tail - head` correct.
    alignas(kCacheLine) std::atomic<std::size_t> head_{0}; // written by the consumer
    std::size_t cachedTail_ = 0;                           // consumer's last view of tail_
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0}; // written by the producer
    std::size_t cachedHead_ = 0;                           // producer's last view of head_
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace proton::core
