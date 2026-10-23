// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/core/spsc_ring.h"
#include "proton/sim/sim_chunk.h"
#include "proton/sim/sim_engine.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace proton::sim {

// Runs an engine on its own thread and hands its chunks to one consumer through an SpscRing
// (ARCHITECTURE.md section 4).
//
// The worker owns the engine. Its thread advances the engine `chunkDuration` seconds of
// simulation time at a time and pushes each chunk into the ring. When the ring is full it waits,
// keeping the chunk, until the consumer makes room (backpressure): nothing is ever dropped. It
// runs until stop() or until the engine fails; it does not pace itself to wall-clock time.
//
// All public functions are for one thread, the consumer's (the UI thread): it starts and stops
// runs and pops chunks. The engine is only touched by the worker thread during a run, and by the
// caller of start() while no run is going. Besides the ring, only control state crosses
// threads: the stop request, and a failure flag published once with the error text.
//
// Settings are sanitised: a non-finite chunkDuration becomes the default, and both settings are
// clamped to the ranges given by the constants below.
class SimWorker
{
public:
    static constexpr double kMinChunkDuration = 1e-12; // s
    static constexpr double kMaxChunkDuration = 1.0;
    static constexpr std::size_t kMaxQueueCapacity = 4096; // chunks

    struct Config
    {
        double chunkDuration = 1e-3;    // s of simulation time per chunk
        std::size_t queueCapacity = 64; // chunks; rounded up to a power of two

        friend bool operator==(const Config&, const Config&) = default;
    };

    // The settings as the worker would apply them.
    static Config sanitise(const Config& config);

    explicit SimWorker(std::unique_ptr<ISimEngine> engine) : SimWorker(std::move(engine), Config{})
    {}
    SimWorker(std::unique_ptr<ISimEngine> engine, const Config& config);
    ~SimWorker(); // stops the run

    SimWorker(const SimWorker&) = delete;
    SimWorker& operator=(const SimWorker&) = delete;

    const Config& config() const { return config_; }

    // Stops any run, drops the chunks it left unread, loads `netlist` into the engine on this
    // thread and starts a run from t = 0. If the load fails, no run starts, it returns false and
    // error() says why.
    bool start(std::string_view netlist);

    // Stops the run and waits for the worker thread to end. Unread chunks stay readable until the
    // next start(), and a chunk the thread was waiting to push is kept for resume(). Does nothing
    // when no run is going.
    void stop();

    // Continues the run that stop() stopped, from where it stopped: the chunks that follow are
    // exactly those an unstopped run would have given. Between stop() and resume() the caller may
    // change the engine (a live change, ADR 0005), since no thread uses it then. Returns false and
    // does nothing if there is no stopped run: none was started, or it failed, or it is running.
    bool resume();

    // The oldest unread chunk, if there is one.
    std::optional<SimChunk> tryPop();

    // True while a run is producing chunks: started, and neither stopped nor failed.
    bool running() const;

    // Why start() or the run failed. Empty until then, and after a successful start().
    const std::string& error() const;

    // The engine's signal names for the current or last run, the order of every chunk's signals.
    const std::vector<std::string>& signalNames() const { return names_; }

private:
    void run(std::stop_token stop);
    bool push(const std::stop_token& stop, SimChunk&& chunk);

    Config config_;
    std::unique_ptr<ISimEngine> engine_;
    core::SpscRing<SimChunk> ring_;
    std::vector<std::string> names_;
    // A chunk the thread was waiting to push when it stopped. Used only by the thread, and by
    // start() while no thread runs.
    std::optional<SimChunk> pending_;
    bool stopped_ = false; // stop() stopped a run that resume() can continue
    // Written by the worker thread before failed_ is set, otherwise only while no thread runs.
    std::string error_;
    std::atomic<bool> failed_{false};
    // The worker thread waits on these for room in the ring; a stop request wakes it at once.
    std::mutex retryMutex_;
    std::condition_variable_any retry_;
    std::jthread thread_; // last: it ends before the members it uses go away
};

} // namespace proton::sim
