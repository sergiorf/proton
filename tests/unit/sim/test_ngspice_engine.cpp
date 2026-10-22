// SPDX-License-Identifier: AGPL-3.0-only
// NgspiceEngine against the fetched ngspice DLL (M1.2). Windows only: built when CMake found
// third_party/ngspice. Its own executable: ngspice keeps process-wide state.
#include "proton/sim/ngspice_engine.h"
#include "proton/sim/sim_worker.h"
#include "proton/test/test.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using proton::sim::indexOfSignal;
using proton::sim::NgspiceEngine;
using proton::sim::SimChunk;
using proton::sim::SimWorker;

namespace {

// tests/golden/integration/rc_example.cir: a 1 kHz sine, 1 V around 0.5 V, into 1 k and 100 n.
constexpr const char* kRc = "* Proton netlist\n"
                            "C1 out 0 100n\n"
                            "R1 in out 1k\n"
                            "* gen1\n"
                            "* function generator gen1\n"
                            "Vgen1 in 0 SIN(500m 1 1k)\n";

constexpr double kChunk = 1e-3; // s

NgspiceEngine::Config shortRun(double stopTime = 20e-3)
{
    NgspiceEngine::Config config;
    config.stopTime = stopTime;
    config.maxStep = 1e-5;
    return config;
}

// Every chunk of a run until `duration` or the run ends, in order.
std::vector<SimChunk> run(NgspiceEngine& engine, double duration, double chunk = kChunk)
{
    std::vector<SimChunk> chunks;
    for (double t = 0.0; t < duration - chunk / 2; t += chunk) {
        chunks.push_back(engine.advance(chunk));
        if (!engine.error().empty()) {
            break;
        }
    }
    return chunks;
}

// The chunks joined into one.
SimChunk join(const std::vector<SimChunk>& chunks)
{
    SimChunk all;
    for (const SimChunk& c : chunks) {
        all.time.insert(all.time.end(), c.time.begin(), c.time.end());
        all.signals.resize(c.signals.size());
        for (std::size_t s = 0; s < c.signals.size(); ++s) {
            all.signals[s].insert(all.signals[s].end(), c.signals[s].begin(), c.signals[s].end());
        }
    }
    return all;
}

bool strictlyIncreasing(const std::vector<double>& t)
{
    for (std::size_t i = 1; i < t.size(); ++i) {
        if (!(t[i] > t[i - 1])) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST(sanitise_clamps_settings)
{
    NgspiceEngine::Config config;
    config.stopTime = -1.0;
    config.maxStep = std::nan("");
    config.queueCapacity = 0;
    const NgspiceEngine::Config s = NgspiceEngine::sanitise(config);
    CHECK_EQ(s.stopTime, NgspiceEngine::kMinStopTime);
    CHECK_EQ(s.maxStep, NgspiceEngine::kMinStopTime); // the default 1 us, at most stopTime
    CHECK_EQ(s.queueCapacity, NgspiceEngine::kMinQueueCapacity);
}

TEST(a_missing_library_is_a_load_error)
{
    NgspiceEngine::Config config = shortRun();
    config.libraryPath = "no-such-dir/ngspice.dll";
    NgspiceEngine engine(config);
    CHECK(!engine.load(kRc));
    CHECK(engine.error().find("ngspice.dll") != std::string::npos);
    CHECK(engine.signalNames().empty());
    CHECK(engine.advance(kChunk).empty());
}

TEST(rc_netlist_streams_strictly_increasing_chunks)
{
    NgspiceEngine engine(shortRun());
    CHECK(engine.load(kRc));
    CHECK_EQ(engine.error(), std::string());
    const auto in = indexOfSignal(engine.signalNames(), "v(in)");
    const auto out = indexOfSignal(engine.signalNames(), "v(out)");
    CHECK(in.has_value());
    CHECK(out.has_value());
    CHECK(indexOfSignal(engine.signalNames(), "i(vgen1)").has_value());
    CHECK(!indexOfSignal(engine.signalNames(), "time").has_value());

    const std::vector<SimChunk> chunks = run(engine, 10e-3);
    CHECK_EQ(chunks.size(), std::size_t{10});
    CHECK_EQ(engine.error(), std::string());
    for (std::size_t k = 0; k < chunks.size(); ++k) {
        const SimChunk& c = chunks[k];
        CHECK(!c.empty());
        CHECK_EQ(c.signals.size(), engine.signalNames().size());
        for (const auto& signal : c.signals) {
            CHECK_EQ(signal.size(), c.time.size());
        }
        // Each chunk holds the points in its own slice of simulation time.
        CHECK(c.time.front() >= static_cast<double>(k) * kChunk);
        CHECK(c.time.back() <= static_cast<double>(k + 1) * kChunk * (1 + 1e-12));
    }
    const SimChunk all = join(chunks);
    CHECK_EQ(all.time.front(), 0.0);
    CHECK(strictlyIncreasing(all.time));
    // No step longer than maxStep.
    for (std::size_t i = 1; i < all.time.size(); ++i) {
        CHECK(all.time[i] - all.time[i - 1] <= 1e-5 * (1 + 1e-9));
    }
    if (in && out) {
        // The DC operating point, then the sine.
        CHECK_NEAR(all.signals[*in].front(), 0.5, 1e-9);
        CHECK_NEAR(all.signals[*out].front(), 0.5, 1e-9);
        for (std::size_t i = 0; i < all.time.size(); ++i) {
            const double expected = 0.5 + std::sin(2 * 3.14159265358979323846 * 1e3 * all.time[i]);
            CHECK_NEAR(all.signals[*in][i], expected, 1e-6);
        }
    }
}

TEST(a_broken_netlist_is_a_load_error)
{
    NgspiceEngine engine(shortRun());
    // ngspice itself only warns that it ignores the line, and would simulate the rest.
    CHECK(!engine.load("* broken\nR1 in\nV1 in 0 DC 1\n"));
    CHECK(engine.error().find("r1 in") != std::string::npos);
    CHECK(engine.signalNames().empty());
    CHECK(engine.advance(kChunk).empty());

    CHECK(!engine.load("* unknown model\nV1 in 0 DC 1\nQ1 in 0 0 nosuchmodel\n"));
    CHECK(!engine.error().empty());

    CHECK(!engine.load("   \n"));
    CHECK(!engine.error().empty());

    // ngspice is still usable afterwards.
    CHECK(engine.load(kRc));
    CHECK_EQ(engine.error(), std::string());
    CHECK(!engine.advance(kChunk).empty());
}

TEST(a_circuit_ngspice_cannot_solve_is_a_load_error)
{
    NgspiceEngine engine(shortRun());
    // Two sources forcing different voltages on one node: there is no operating point.
    CHECK(!engine.load("* conflict\nV1 in 0 DC 1\nV2 in 0 DC 2\nR1 in 0 1k\n"));
    CHECK(!engine.error().empty());
    CHECK(engine.signalNames().empty());
    CHECK(engine.load(kRc));
}

TEST(the_run_ends_at_the_stop_time)
{
    NgspiceEngine engine(shortRun(5e-3));
    CHECK(engine.load(kRc));
    const std::vector<SimChunk> chunks = run(engine, 20e-3);
    CHECK_EQ(chunks.size(), std::size_t{5});
    CHECK(engine.error().find("stop time") != std::string::npos);
    const SimChunk all = join(chunks);
    CHECK(strictlyIncreasing(all.time));
    CHECK_NEAR(all.time.back(), 5e-3, 1e-12);
    CHECK(engine.advance(kChunk).empty());
}

TEST(reset_restarts_the_same_run)
{
    NgspiceEngine engine(shortRun());
    CHECK(engine.load(kRc));
    const SimChunk first = join(run(engine, 5e-3));
    engine.reset();
    CHECK_EQ(engine.error(), std::string());
    const SimChunk second = join(run(engine, 5e-3));
    CHECK(!first.empty());
    CHECK(first == second);
}

TEST(only_one_instance_runs_at_a_time)
{
    {
        NgspiceEngine first(shortRun());
        CHECK(first.load(kRc));
        NgspiceEngine second(shortRun());
        CHECK(!second.load(kRc));
        CHECK(second.error().find("one") != std::string::npos);
        CHECK(!first.advance(kChunk).empty()); // the first is unaffected
    }
    NgspiceEngine third(shortRun());
    CHECK(third.load(kRc));
}

TEST(backpressure_keeps_every_point)
{
    SimChunk expected;
    {
        NgspiceEngine engine(shortRun());
        CHECK(engine.load(kRc));
        expected = join(run(engine, 10e-3));
    }
    NgspiceEngine::Config config = shortRun();
    config.queueCapacity = 1;
    NgspiceEngine engine(config);
    CHECK(engine.load(kRc));
    std::vector<SimChunk> chunks;
    for (int k = 0; k < 10; ++k) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // ngspice waits meanwhile
        chunks.push_back(engine.advance(kChunk));
    }
    CHECK(!expected.empty());
    CHECK(join(chunks) == expected);
}

TEST(load_halts_a_run_blocked_on_a_full_queue)
{
    NgspiceEngine::Config config = shortRun();
    config.queueCapacity = 1;
    NgspiceEngine engine(config);
    CHECK(engine.load(kRc));
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); // ngspice blocks in its callback
    CHECK(engine.load("* divider\nV1 in 0 DC 2\nR1 in out 1k\nR2 out 0 1k\n"));
    const auto out = indexOfSignal(engine.signalNames(), "v(out)");
    CHECK(out.has_value());
    const SimChunk c = engine.advance(kChunk);
    CHECK(!c.empty());
    if (out && !c.empty()) {
        CHECK_NEAR(c.signals[*out].front(), 1.0, 1e-9);
    }
}

TEST(runs_under_the_sim_worker)
{
    SimWorker worker(std::make_unique<NgspiceEngine>(shortRun()));
    CHECK(worker.start(kRc));
    CHECK(indexOfSignal(worker.signalNames(), "v(out)").has_value());
    std::vector<SimChunk> chunks;
    // The run ends at 20 ms: the worker then stops with the engine's message.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::optional<SimChunk> c = worker.tryPop()) {
            chunks.push_back(std::move(*c));
        } else if (!worker.running()) {
            break;
        } else {
            std::this_thread::yield();
        }
    }
    while (std::optional<SimChunk> c = worker.tryPop()) {
        chunks.push_back(std::move(*c));
    }
    CHECK(!worker.running());
    CHECK(worker.error().find("stop time") != std::string::npos);
    const SimChunk all = join(chunks);
    CHECK(strictlyIncreasing(all.time));
    CHECK_EQ(all.time.front(), 0.0);
    CHECK_NEAR(all.time.back(), 20e-3, 1e-12);
}

// Netlist errors that ngspice 46 treats as fatal: it calls its exit callback (ADR 0002).
constexpr const char* kFatal[] = {
    "* missing include\n.include no-such-file.lib\nV1 in 0 DC 1\nR1 in 0 1k\n",
    "* missing ends\n.subckt half a b\nR1 a b 1k\nV1 in 0 DC 1\nR2 in 0 1k\n",
    "* bad param\n.param x={1+}\nV1 in 0 DC {x}\nR1 in 0 1k\n",
};

TEST(a_fatal_error_is_a_load_error_and_ngspice_is_reloaded)
{
    NgspiceEngine engine(shortRun());
    CHECK(engine.load(kRc));
    const SimChunk before = join(run(engine, 5e-3));
    CHECK(!before.empty());
    for (const char* netlist : kFatal) {
        CHECK(!engine.load(netlist));
        CHECK(engine.error().find("fatal error") != std::string::npos);
        CHECK(engine.signalNames().empty());
        CHECK(engine.advance(kChunk).empty());
        // The next load gets a fresh ngspice, which runs the circuit exactly as before.
        CHECK(engine.load(kRc));
        CHECK_EQ(engine.error(), std::string());
        CHECK(join(run(engine, 5e-3)) == before);
    }
    CHECK(!engine.load(kFatal[0]));
    CHECK(engine.error().find("no-such-file.lib") != std::string::npos);
}

TEST(fatal_errors_one_after_another)
{
    NgspiceEngine engine(shortRun());
    for (int k = 0; k < 10; ++k) {
        CHECK(!engine.load(kFatal[k % 3]));
        CHECK(engine.error().find("fatal error") != std::string::npos);
    }
    CHECK(engine.load(kRc));
    CHECK(!engine.advance(kChunk).empty());
}

TEST(a_fatal_error_under_the_sim_worker)
{
    SimWorker worker(std::make_unique<NgspiceEngine>(shortRun()));
    CHECK(!worker.start(kFatal[0]));
    CHECK(!worker.running());
    CHECK(worker.error().find("fatal error") != std::string::npos);
    CHECK(worker.start(kRc));
    CHECK_EQ(worker.error(), std::string());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    std::optional<SimChunk> chunk;
    while (!chunk && std::chrono::steady_clock::now() < deadline) {
        chunk = worker.tryPop();
        if (!chunk) {
            std::this_thread::yield();
        }
    }
    CHECK(chunk.has_value());
    if (chunk) {
        CHECK_EQ(chunk->time.front(), 0.0);
    }
}
