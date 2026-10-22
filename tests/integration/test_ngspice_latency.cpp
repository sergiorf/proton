// SPDX-License-Identifier: AGPL-3.0-only
// Circuit-to-frame latency and live changes with ngspice (M1.5, ADR 0005). The RC example runs on
// a SimWorker with its default chunks and queue; a scope on this thread pops and feeds every chunk
// as soon as it arrives. Measured in wall-clock time, from the request to the first scope frame
// that shows it:
//
// - the first start, which loads ngspice;
// - a generator frequency change by restarting the run with the new netlist;
// - the same change by halting, `alter` and resuming, while the scope keeps up with the run, and
//   when the worker's ring and the engine's queue are full of points from before the change.
//
// Prints what it measures; ADR 0005 records it from a Release build. It checks that every change
// reaches the scope, with the new frequency and, for alter, without losing a point; the timings
// themselves are checked only loosely, since CI runners are slow and noisy.
#include "proton/instruments/scope.h"
#include "proton/schematic/netlist.h"
#include "proton/sim/ngspice_engine.h"
#include "proton/sim/sim_worker.h"
#include "proton/spike/pipeline.h"
#include "proton/test/test.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

using proton::instruments::Scope;
using proton::instruments::ScopeFrame;
using proton::sim::NgspiceEngine;
using proton::sim::SimChunk;
using proton::sim::SimWorker;

namespace {

using Clock = std::chrono::steady_clock;
using Ms = std::chrono::duration<double, std::milli>;

constexpr int kRepeats = 10;
constexpr double kLow = 1e3; // Hz, the two generator frequencies a change goes between
constexpr double kHigh = 2e3;
// A change that has not reached the scope by then is a failure.
constexpr std::chrono::seconds kTimeout{10};
// Long enough for the ring (64 chunks of 1 ms) and the engine queue to fill at 1 us steps.
constexpr std::chrono::milliseconds kFillTime{400};
// What the changes must stay well under to count as reaching the scope at all (CI is slow); D3's
// budget is about 100 ms, judged on the Release numbers ADR 0005 records.
constexpr double kLooseBoundMs = 2000.0;

proton::spike::RcExample example(double frequency)
{
    proton::spike::RcExample rc;
    rc.generator.frequency = frequency;
    return rc;
}

std::string netlist(double frequency)
{
    return proton::schematic::buildNetlist(proton::spike::buildCircuit(example(frequency)));
}

// The alter change that sets the example's generator to `frequency`: SIN(offset amplitude f).
std::string alterTo(double frequency)
{
    const auto generator = example(frequency).generator;
    return "@vgen1[sin] = [ " + std::to_string(generator.offset) + " " +
           std::to_string(generator.amplitude) + " " + std::to_string(frequency) + " ]";
}

// Whether every CH1 sample of `frame` lies on the example's generator sine at `frequency`.
bool shows(const ScopeFrame& frame, double frequency)
{
    const auto generator = example(frequency).generator;
    const std::vector<double>& ch1 = frame.values[0];
    for (std::size_t i = 0; i < ch1.size(); ++i) {
        const double t =
            frame.triggerTime + frame.startTime + static_cast<double>(i) * frame.sampleInterval;
        const double expected =
            generator.offset + generator.amplitude * std::sin(2 * std::numbers::pi * frequency * t);
        if (std::abs(ch1[i] - expected) > 0.02) {
            return false;
        }
    }
    return !ch1.empty();
}

// The pump: pops every chunk the worker has and feeds the scope, as the UI thread will.
class Pump
{
public:
    Pump(SimWorker& worker, std::size_t in, std::size_t out)
        : worker_(worker), in_(in), out_(out),
          config_(proton::spike::defaultSettings(example(kLow)).scope), scope_(config_)
    {}

    // Starts acquisition again, as after a restart: simulation time begins at 0 again.
    void restartScope() { scope_.configure(config_); }

    // Pumps until a frame shows `frequency`, and returns when that was, or nothing on timeout.
    // `stale` counts the frames before it.
    std::optional<Clock::time_point> until(double frequency, int* stale = nullptr)
    {
        const Clock::time_point deadline = Clock::now() + kTimeout;
        while (Clock::now() < deadline) {
            std::optional<SimChunk> chunk = worker_.tryPop();
            if (!chunk) {
                if (!worker_.running()) {
                    return std::nullopt;
                }
                std::this_thread::yield();
                continue;
            }
            check(*chunk);
            scope_.feed(chunk->time, chunk->signals[in_], chunk->signals[out_]);
            while (std::optional<ScopeFrame> frame = scope_.popFrame()) {
                if (shows(*frame, frequency)) {
                    return Clock::now();
                }
                if (stale != nullptr) {
                    ++*stale;
                }
            }
        }
        return std::nullopt;
    }

    // Times must continue from chunk to chunk, with no step longer than maxStep (no point lost).
    void reset(double maxStep)
    {
        maxStep_ = maxStep;
        lastTime_.reset();
    }
    int gaps() const { return gaps_; }

private:
    void check(const SimChunk& chunk)
    {
        for (const double t : chunk.time) {
            if (lastTime_ && (!(t > *lastTime_) || t - *lastTime_ > maxStep_ * (1 + 1e-9))) {
                ++gaps_;
            }
            lastTime_ = t;
        }
    }

    SimWorker& worker_;
    std::size_t in_;
    std::size_t out_;
    Scope::Config config_;
    Scope scope_;
    double maxStep_ = 0.0;
    std::optional<double> lastTime_;
    int gaps_ = 0;
};

struct Stats
{
    double median = 0.0;
    double max = 0.0;
};

Stats stats(std::vector<double> ms)
{
    if (ms.empty()) {
        return {};
    }
    std::sort(ms.begin(), ms.end());
    return {ms[ms.size() / 2], ms.back()};
}

void print(const char* what, double maxStep, const std::vector<double>& ms)
{
    const Stats s = stats(ms);
    std::printf("maxStep %g us, %s: median %.1f ms, max %.1f ms (%zu runs)\n", maxStep * 1e6, what,
                s.median, s.max, ms.size());
}

// Every measurement at one maximum step, on one worker.
void measure(double maxStep)
{
    NgspiceEngine::Config config;
    // Shorter than the default 10 s: ngspice reserves its plot for the whole run up front
    // (ADR 0005), and nothing here runs for long.
    config.stopTime = 2.0;
    config.maxStep = maxStep;
    auto owned = std::make_unique<NgspiceEngine>(config);
    NgspiceEngine& engine = *owned; // changed only while the worker is stopped
    SimWorker worker(std::move(owned));

    // First start: loads ngspice.dll.
    Clock::time_point before = Clock::now();
    CHECK(worker.start(netlist(kLow)));
    const Clock::time_point started = Clock::now();
    const auto in = proton::sim::indexOfSignal(worker.signalNames(), "v(in)");
    const auto out = proton::sim::indexOfSignal(worker.signalNames(), "v(out)");
    CHECK(in.has_value() && out.has_value());
    if (!in || !out) {
        return;
    }
    Pump pump(worker, *in, *out);
    pump.reset(maxStep);
    std::optional<Clock::time_point> frame = pump.until(kLow);
    CHECK(frame.has_value());
    if (frame) {
        std::printf("maxStep %g us, first start: start() %.1f ms, first frame %.1f ms\n",
                    maxStep * 1e6, Ms(started - before).count(), Ms(*frame - before).count());
    }

    // The changes, each from both states: the scope keeping up with the run, and nothing pumped
    // for a while so that the worker's ring and the engine's queue are full.
    double frequency = kLow;
    for (const bool alter : {false, true}) {
        for (const bool full : {false, true}) {
            std::vector<double> stopped;
            std::vector<double> applied;
            std::vector<double> shown;
            std::vector<double> staleFrames;
            for (int k = 0; k < kRepeats; ++k) {
                if (full) {
                    std::this_thread::sleep_for(kFillTime);
                }
                frequency = frequency == kLow ? kHigh : kLow;
                before = Clock::now();
                worker.stop();
                const Clock::time_point afterStop = Clock::now();
                if (alter) {
                    // Halt, alter, resume: points from before the change are still on their way.
                    CHECK_EQ(engine.alter(alterTo(frequency)), std::string());
                    CHECK(worker.resume());
                } else {
                    // Restart: a new netlist, a new run from t = 0, a new acquisition.
                    CHECK(worker.start(netlist(frequency)));
                    pump.restartScope();
                    pump.reset(maxStep);
                }
                const Clock::time_point afterChange = Clock::now();
                int stale = 0;
                frame = pump.until(frequency, &stale);
                CHECK(frame.has_value());
                if (frame) {
                    stopped.push_back(Ms(afterStop - before).count());
                    applied.push_back(Ms(afterChange - before).count());
                    shown.push_back(Ms(*frame - before).count());
                    staleFrames.push_back(stale);
                }
            }
            std::printf("maxStep %g us, %s, %s:\n", maxStep * 1e6, alter ? "alter" : "restart",
                        full ? "queues full" : "scope keeping up");
            print("  stop()", maxStep, stopped);
            print(alter ? "  + alter() + resume()" : "  + start()", maxStep, applied);
            print("  to frame", maxStep, shown);
            const Stats s = stats(staleFrames);
            std::printf("  frames from before the change: median %.0f, max %.0f\n", s.median,
                        s.max);
            CHECK(stats(shown).median < kLooseBoundMs);
        }
    }
    worker.stop();
    CHECK_EQ(pump.gaps(), 0);
}

} // namespace

TEST(latency_at_the_engine_default_step)
{
    measure(NgspiceEngine::Config{}.maxStep); // 1 us
}

TEST(latency_at_the_pipeline_step)
{
    measure(proton::spike::PipelineSettings{}.maxStep); // 10 us
}
