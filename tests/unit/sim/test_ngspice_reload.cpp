// SPDX-License-Identifier: AGPL-3.0-only
// NgspiceEngine after a fatal error: ngspice is unloaded and loaded afresh (M1.4). Its own
// executable, apart from test_ngspice_engine.cpp, because every reload leaks ngspice's memory
// from a library that is gone by the time LeakSanitizer looks, so its suppressions cannot name
// it: CMake turns leak detection off for this executable only (ADR 0008).
#include "proton/sim/ngspice_engine.h"
#include "proton/sim/sim_worker.h"
#include "proton/test/test.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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

} // namespace

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
