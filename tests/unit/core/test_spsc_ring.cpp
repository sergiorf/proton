// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/core/spsc_ring.h"

#include "proton/test/test.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <thread>

using proton::core::SpscRing;

namespace {

// Counts live instances so tests can see when the ring destroys elements.
struct Tracked
{
    static inline int live = 0;

    explicit Tracked(int v) : value(v) { ++live; }
    Tracked(Tracked&& other) noexcept : value(other.value) { ++live; }
    Tracked& operator=(Tracked&&) noexcept = default;
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    ~Tracked() { --live; }

    int value;
};

} // namespace

TEST(spsc_capacity_rounds_up_to_power_of_two)
{
    CHECK_EQ(SpscRing<int>(0).capacity(), 1);
    CHECK_EQ(SpscRing<int>(1).capacity(), 1);
    CHECK_EQ(SpscRing<int>(3).capacity(), 4);
    CHECK_EQ(SpscRing<int>(64).capacity(), 64);
    CHECK_EQ(SpscRing<int>(65).capacity(), 128);
}

TEST(spsc_pop_on_empty_returns_nothing)
{
    SpscRing<int> ring(4);
    CHECK(!ring.tryPop().has_value());
}

TEST(spsc_is_fifo_and_full_push_leaves_value_untouched)
{
    SpscRing<std::unique_ptr<int>> ring(4);
    for (int i = 0; i < 4; ++i) {
        CHECK(ring.tryPush(std::make_unique<int>(i)));
    }
    auto extra = std::make_unique<int>(99);
    CHECK(!ring.tryPush(std::move(extra)));
    CHECK(extra != nullptr); // not moved from on failure
    CHECK_EQ(*extra, 99);

    for (int i = 0; i < 4; ++i) {
        std::optional<std::unique_ptr<int>> item = ring.tryPop();
        CHECK(item.has_value() && *item != nullptr);
        if (item && *item) {
            CHECK_EQ(**item, i);
        }
    }
    CHECK(!ring.tryPop().has_value());
}

TEST(spsc_wraps_around_many_times)
{
    SpscRing<int> ring(4);
    int next = 0;
    int expected = 0;
    for (int round = 0; round < 1000; ++round) {
        for (int i = 0; i < 3; ++i) {
            CHECK(ring.tryPush(int{next++}));
        }
        for (int i = 0; i < 3; ++i) {
            const std::optional<int> item = ring.tryPop();
            CHECK(item.has_value());
            CHECK_EQ(item.value_or(-1), expected++);
        }
    }
    CHECK(!ring.tryPop().has_value());
}

TEST(spsc_destroys_popped_and_remaining_elements)
{
    Tracked::live = 0;
    {
        SpscRing<Tracked> ring(4);
        CHECK(ring.tryPush(Tracked{1}));
        CHECK(ring.tryPush(Tracked{2}));
        CHECK(ring.tryPush(Tracked{3}));
        CHECK_EQ(Tracked::live, 3);
        {
            const std::optional<Tracked> item = ring.tryPop();
            CHECK_EQ(item.has_value() ? item->value : -1, 1);
            CHECK_EQ(Tracked::live, 3); // the popped one lives in `item`, its slot is empty
        }
        CHECK_EQ(Tracked::live, 2);
    }
    CHECK_EQ(Tracked::live, 0);
}

// Two threads, 10^6 heap-allocated items through a small ring so it is full and empty often.
// Run under TSan (linux-clang-tsan), this also checks that the payload written by the producer
// is published to the consumer.
TEST(spsc_two_thread_stress_keeps_order_and_count)
{
    constexpr std::uint64_t kItems = 1'000'000;
    SpscRing<std::unique_ptr<std::uint64_t>> ring(64);

    std::thread producer([&ring] {
        for (std::uint64_t i = 0; i < kItems; ++i) {
            auto item = std::make_unique<std::uint64_t>(i);
            while (!ring.tryPush(std::move(item))) {
                std::this_thread::yield();
            }
        }
    });

    std::uint64_t received = 0;
    std::uint64_t outOfOrder = 0;
    while (received < kItems) {
        std::optional<std::unique_ptr<std::uint64_t>> item = ring.tryPop();
        if (!item) {
            std::this_thread::yield();
            continue;
        }
        if (!*item || **item != received) {
            ++outOfOrder;
        }
        ++received;
    }
    producer.join();

    CHECK_EQ(received, kItems);
    CHECK_EQ(outOfOrder, 0);
    CHECK(!ring.tryPop().has_value());
}
