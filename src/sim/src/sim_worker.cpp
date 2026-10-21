// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/sim/sim_worker.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace proton::sim {

namespace {

// A full ring is retried with a few yields first, then with short sleeps. The wait is only
// scheduling: it never changes simulation time or the chunks.
constexpr int kYieldsBeforeSleep = 16;
constexpr std::chrono::microseconds kRetrySleep{100};

const std::string kNoError;

} // namespace

SimWorker::Config SimWorker::sanitise(const Config& config)
{
    const Config defaults;
    Config result;
    result.chunkDuration = std::clamp(std::isfinite(config.chunkDuration) ? config.chunkDuration
                                                                          : defaults.chunkDuration,
                                      kMinChunkDuration, kMaxChunkDuration);
    result.queueCapacity = std::clamp(config.queueCapacity, std::size_t{1}, kMaxQueueCapacity);
    return result;
}

SimWorker::SimWorker(std::unique_ptr<ISimEngine> engine, const Config& config)
    : config_(sanitise(config)), engine_(std::move(engine)), ring_(config_.queueCapacity)
{}

SimWorker::~SimWorker()
{
    stop();
}

bool SimWorker::start(std::string_view netlist)
{
    stop();
    while (ring_.tryPop()) {
    }
    failed_.store(false, std::memory_order_relaxed);
    error_.clear();
    names_.clear();
    if (!engine_) {
        error_ = "no simulation engine";
        return false;
    }
    if (!engine_->load(netlist)) {
        error_ = engine_->error().empty() ? "the netlist did not load" : engine_->error();
        return false;
    }
    names_ = engine_->signalNames();
    // Starting the thread orders everything above before anything the thread does.
    thread_ = std::jthread([this](std::stop_token stop) { run(std::move(stop)); });
    return true;
}

void SimWorker::stop()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join(); // orders everything the thread did before what follows
    }
}

std::optional<SimChunk> SimWorker::tryPop()
{
    return ring_.tryPop();
}

bool SimWorker::running() const
{
    // stop() joins, so a joinable thread is one that has not been stopped.
    return thread_.joinable() && !failed_.load(std::memory_order_acquire);
}

const std::string& SimWorker::error() const
{
    // While the thread runs, error_ is the thread's until it publishes a failure.
    if (thread_.joinable() && !failed_.load(std::memory_order_acquire)) {
        return kNoError;
    }
    return error_;
}

void SimWorker::run(std::stop_token stop)
{
    while (!stop.stop_requested()) {
        SimChunk chunk = engine_->advance(config_.chunkDuration);
        const bool failed = !engine_->error().empty();
        if (!chunk.empty() && !push(stop, std::move(chunk))) {
            return; // stopped while waiting for room
        }
        if (failed) {
            error_ = engine_->error();
            failed_.store(true, std::memory_order_release);
            return;
        }
    }
}

bool SimWorker::push(const std::stop_token& stop, SimChunk&& chunk)
{
    for (int attempt = 0;; ++attempt) {
        if (ring_.tryPush(std::move(chunk))) { // a failed push leaves the chunk untouched
            return true;
        }
        if (stop.stop_requested()) {
            return false;
        }
        if (attempt < kYieldsBeforeSleep) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(kRetrySleep);
        }
    }
}

} // namespace proton::sim
