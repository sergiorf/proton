// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/sim/mock_engine.h"
#include "proton/sim/sim_worker.h"
#include "proton/test/test.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using proton::sim::ISimEngine;
using proton::sim::MockEngine;
using proton::sim::SimChunk;
using proton::sim::SimWorker;

namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kNetlist = "RC low-pass\nV1 in 0 SIN(0 1 1k)\nR1 in out 1k\nC1 out 0 100n\n";
// Generous: sanitizer builds on CI runners are slow.
constexpr std::chrono::seconds kTimeout{20};

// A MockEngine that counts its advance() calls, readable from the test thread.
class CountingEngine final : public ISimEngine
{
public:
    explicit CountingEngine(std::atomic<int>& advances, const MockEngine::Config& config = {})
        : mock_(config), advances_(advances)
    {}

    bool load(std::string_view netlist) override { return mock_.load(netlist); }
    const std::string& error() const override { return mock_.error(); }
    const std::vector<std::string>& signalNames() const override { return mock_.signalNames(); }
    SimChunk advance(double duration) override
    {
        advances_.fetch_add(1, std::memory_order_relaxed);
        return mock_.advance(duration);
    }
    void reset() override { mock_.reset(); }

private:
    MockEngine mock_;
    std::atomic<int>& advances_;
};

std::unique_ptr<ISimEngine> mock(const MockEngine::Config& config = {})
{
    return std::make_unique<MockEngine>(config);
}

SimWorker::Config withCapacity(std::size_t capacity)
{
    SimWorker::Config config;
    config.queueCapacity = capacity;
    return config;
}

// Waits until `done` is true, or the timeout. Returns `done()`.
bool waitFor(const std::function<bool()>& done)
{
    const Clock::time_point deadline = Clock::now() + kTimeout;
    while (!done()) {
        if (Clock::now() > deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

// Pops `count` chunks, waiting for each; fewer if the timeout comes first.
std::vector<SimChunk> pop(SimWorker& worker, std::size_t count)
{
    std::vector<SimChunk> chunks;
    while (chunks.size() < count) {
        std::optional<SimChunk> chunk;
        if (!waitFor([&] { return (chunk = worker.tryPop()).has_value(); })) {
            break;
        }
        chunks.push_back(std::move(*chunk));
    }
    return chunks;
}

// Every chunk left in the ring of a worker that has stopped producing.
std::vector<SimChunk> drain(SimWorker& worker)
{
    std::vector<SimChunk> chunks;
    while (std::optional<SimChunk> chunk = worker.tryPop()) {
        chunks.push_back(std::move(*chunk));
    }
    return chunks;
}

// The chunks a MockEngine gives when advanced directly, skipping empty ones as the worker does.
std::vector<SimChunk> direct(const MockEngine::Config& config, double chunkDuration,
                             std::size_t count)
{
    MockEngine engine(config);
    CHECK(engine.load(kNetlist));
    std::vector<SimChunk> chunks;
    while (chunks.size() < count && engine.error().empty()) {
        SimChunk chunk = engine.advance(chunkDuration);
        if (!chunk.empty()) {
            chunks.push_back(std::move(chunk));
        }
    }
    return chunks;
}

} // namespace

TEST(settings_are_sanitised)
{
    CHECK(SimWorker::sanitise(SimWorker::Config{}) == SimWorker::Config{});
    SimWorker::Config bad;
    bad.chunkDuration = std::numeric_limits<double>::quiet_NaN();
    bad.queueCapacity = 0;
    SimWorker::Config fixed = SimWorker::sanitise(bad);
    CHECK_EQ(fixed.chunkDuration, SimWorker::Config{}.chunkDuration);
    CHECK_EQ(fixed.queueCapacity, 1u);
    bad.chunkDuration = 100.0;
    bad.queueCapacity = 1u << 20;
    fixed = SimWorker::sanitise(bad);
    CHECK_EQ(fixed.chunkDuration, SimWorker::kMaxChunkDuration);
    CHECK_EQ(fixed.queueCapacity, SimWorker::kMaxQueueCapacity);
}

TEST(nothing_runs_before_start)
{
    SimWorker worker(mock());
    CHECK(!worker.running());
    CHECK(worker.error().empty());
    CHECK(!worker.tryPop().has_value());
    worker.stop(); // harmless
    CHECK(!worker.running());
}

TEST(chunks_come_through_the_thread_unchanged)
{
    SimWorker worker(mock());
    CHECK(worker.start(kNetlist));
    CHECK(worker.running());
    CHECK(worker.signalNames() == std::vector<std::string>{"v(in)", "v(out)"});
    const std::vector<SimChunk> chunks = pop(worker, 200);
    worker.stop();
    CHECK(!worker.running());
    CHECK_EQ(chunks.size(), 200u);
    CHECK(chunks == direct({}, worker.config().chunkDuration, 200));
}

TEST(a_full_queue_holds_the_worker_back)
{
    std::atomic<int> advances{0};
    SimWorker worker(std::make_unique<CountingEngine>(advances), withCapacity(4));
    CHECK(worker.start(kNetlist));
    // The worker fills the ring and then waits, holding one more chunk.
    CHECK(waitFor([&] { return advances.load() >= 5; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_EQ(advances.load(), 5);
    CHECK(worker.running());

    // Nothing was dropped: popping lets it continue exactly where it waited.
    const std::vector<SimChunk> chunks = pop(worker, 100);
    worker.stop();
    CHECK(chunks == direct({}, worker.config().chunkDuration, 100));
}

TEST(stop_returns_while_the_worker_waits_for_room)
{
    std::atomic<int> advances{0};
    SimWorker worker(std::make_unique<CountingEngine>(advances), withCapacity(1));
    CHECK(worker.start(kNetlist));
    CHECK(waitFor([&] { return advances.load() >= 2; }));
    const Clock::time_point before = Clock::now();
    worker.stop();
    CHECK(Clock::now() - before < std::chrono::seconds(5));
    CHECK(!worker.running());
    // The chunk it pushed stays readable until the next start.
    CHECK(worker.tryPop().has_value());
}

TEST(stop_and_restart_1000_times)
{
    SimWorker worker(mock(), withCapacity(8));
    int failures = 0;
    for (int k = 0; k < 1000; ++k) {
        if (!worker.start(kNetlist)) {
            ++failures;
            continue;
        }
        // Every run starts at t = 0: no chunk from the run before survives a restart. Half the
        // runs are stopped without reading anything, some while the ring is full.
        if (k % 2 == 0) {
            const std::vector<SimChunk> first = pop(worker, 1 + static_cast<std::size_t>(k % 7));
            if (first.empty() || first.front().time.front() != 0.0) {
                ++failures;
            }
        } else if (k % 3 == 0) {
            std::this_thread::yield();
        }
        worker.stop();
        if (worker.running()) {
            ++failures;
        }
    }
    CHECK_EQ(failures, 0);
}

TEST(resume_continues_a_stopped_run_exactly)
{
    // Stopped 300 times, often while the worker holds a chunk it cannot push into the full ring,
    // the run still gives exactly the chunks of an unstopped one.
    SimWorker worker(mock(), withCapacity(2));
    CHECK(!worker.resume()); // nothing to resume yet
    CHECK(worker.start(kNetlist));
    CHECK(!worker.resume()); // running
    std::vector<SimChunk> chunks;
    int failures = 0;
    for (int k = 0; k < 300; ++k) {
        for (SimChunk& chunk : pop(worker, static_cast<std::size_t>(k % 3))) {
            chunks.push_back(std::move(chunk));
        }
        if (k % 4 == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(200)); // let the ring fill
        }
        worker.stop();
        if (worker.running() || !worker.resume() || !worker.running()) {
            ++failures;
        }
    }
    for (SimChunk& chunk : pop(worker, 50)) {
        chunks.push_back(std::move(chunk));
    }
    worker.stop();
    CHECK_EQ(failures, 0);
    CHECK(chunks == direct({}, worker.config().chunkDuration, chunks.size()));
    CHECK_EQ(chunks.size(), 300u + 50u);

    // A new start() forgets the stopped run.
    CHECK(worker.start(kNetlist));
    worker.stop();
    CHECK(worker.start(kNetlist));
    const std::vector<SimChunk> first = pop(worker, 1);
    CHECK(!first.empty() && first.front().time.front() == 0.0);
}

TEST(a_failed_run_cannot_be_resumed)
{
    MockEngine::Config config;
    config.failAt = 5e-3;
    SimWorker worker(mock(config));
    CHECK(worker.start(kNetlist));
    CHECK(waitFor([&] { return !worker.running(); }));
    worker.stop();
    CHECK(!worker.resume());
    CHECK(!worker.running());
}

TEST(a_load_error_reaches_error_and_starts_nothing)
{
    SimWorker worker(mock());
    CHECK(!worker.start("   \n"));
    CHECK_EQ(worker.error(), std::string("empty netlist"));
    CHECK(!worker.running());
    CHECK(!worker.tryPop().has_value());
    CHECK(worker.signalNames().empty());

    // A good start clears it.
    CHECK(worker.start(kNetlist));
    CHECK(worker.error().empty());
    CHECK(!pop(worker, 1).empty());
}

TEST(an_engine_error_reaches_error_without_crashing)
{
    MockEngine::Config config;
    config.failAt = 5.5e-3;
    SimWorker worker(mock(config));
    CHECK(worker.start(kNetlist));
    CHECK(waitFor([&] { return !worker.running(); }));
    CHECK_EQ(worker.error(), std::string(MockEngine::kFailure));

    // Every point before the failure was delivered: 5 whole chunks and the half before failAt.
    const std::vector<SimChunk> chunks = drain(worker);
    CHECK_EQ(chunks.size(), 6u);
    CHECK(chunks == direct(config, worker.config().chunkDuration, 6));
    CHECK(chunks.back().time.back() <= 5.5e-3);

    // The error stays after stop(), and a new start clears it.
    worker.stop();
    CHECK_EQ(worker.error(), std::string(MockEngine::kFailure));
    CHECK(worker.start(kNetlist));
    CHECK(worker.error().empty());
    const std::vector<SimChunk> again = pop(worker, 1);
    CHECK(!again.empty() && again.front().time.front() == 0.0);
}

TEST(an_engine_error_while_the_queue_is_full_still_arrives)
{
    MockEngine::Config config;
    config.failAt = 9.5e-3;
    SimWorker worker(mock(config), withCapacity(2));
    CHECK(worker.start(kNetlist));
    std::vector<SimChunk> chunks;
    CHECK(waitFor([&] {
        while (std::optional<SimChunk> chunk = worker.tryPop()) {
            chunks.push_back(std::move(*chunk));
        }
        return !worker.running();
    }));
    for (SimChunk& chunk : drain(worker)) {
        chunks.push_back(std::move(chunk));
    }
    CHECK_EQ(worker.error(), std::string(MockEngine::kFailure));
    CHECK(chunks == direct(config, worker.config().chunkDuration, 10));
}

TEST(destroying_a_running_worker_stops_it)
{
    std::atomic<int> advances{0};
    {
        SimWorker worker(std::make_unique<CountingEngine>(advances), withCapacity(2));
        CHECK(worker.start(kNetlist));
        CHECK(waitFor([&] { return advances.load() >= 1; }));
    }
    const int after = advances.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_EQ(advances.load(), after);
}

TEST(a_worker_without_an_engine_reports_it)
{
    SimWorker worker(nullptr);
    CHECK(!worker.start(kNetlist));
    CHECK_EQ(worker.error(), std::string("no simulation engine"));
    CHECK(!worker.running());
}
