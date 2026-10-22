// SPDX-License-Identifier: AGPL-3.0-only
// Memory of NgspiceEngine over many runs (M1.4): repeated loads and resets, and repeated fatal
// errors that reload the DLL. Windows only, like every ngspice test, and its own executable so
// that nothing else allocates in the process meanwhile. Prints what it measures; ADR 0002 records
// it.
#include "proton/sim/ngspice_engine.h"
#include "proton/test/test.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// psapi.h needs windows.h first.
#include <psapi.h>

#include <cstdio>

using proton::sim::NgspiceEngine;

namespace {

// tests/golden/integration/rc_example.cir
constexpr const char* kRc = "* Proton netlist\n"
                            "C1 out 0 100n\n"
                            "R1 in out 1k\n"
                            "* gen1\n"
                            "* function generator gen1\n"
                            "Vgen1 in 0 SIN(500m 1 1k)\n";

// A missing .include is fatal to ngspice: it calls its exit callback.
constexpr const char* kFatal = "* missing include\n.include no-such-file.lib\nV1 in 0 DC 1\n";

constexpr double kKiB = 1024.0;

// Runs short enough that one advance() takes all of them: a run that has ended needs no bg_halt,
// which takes over 100 ms (ADR 0002), so a thousand cycles take seconds instead of minutes.
NgspiceEngine::Config shortRun()
{
    NgspiceEngine::Config config;
    config.stopTime = 1e-4;
    config.maxStep = 1e-5;
    return config;
}

// Committed memory private to the process, in KiB.
double privateKiB()
{
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                         sizeof(counters));
    return static_cast<double>(counters.PrivateUsage) / kKiB;
}

// Runs `cycle` warmUp times, then `cycles` times, and returns the growth per cycle in KiB over
// the second half of them, after printing what it measured.
template <typename Cycle>
double growthPerCycle(const char* label, int warmUp, int cycles, Cycle cycle)
{
    for (int k = 0; k < warmUp; ++k) {
        cycle(k);
    }
    const double warm = privateKiB();
    for (int k = 0; k < cycles / 2; ++k) {
        cycle(k);
    }
    const double half = privateKiB();
    for (int k = 0; k < cycles - cycles / 2; ++k) {
        cycle(k);
    }
    const double end = privateKiB();
    const double perCycle = (end - half) / (cycles - cycles / 2);
    std::printf("%s: %.0f KiB after %d warm-up cycles, +%.0f KiB after %d more, +%.0f KiB after "
                "%d more (%.2f KiB per cycle)\n",
                label, warm, warmUp, half - warm, cycles / 2, end - half, cycles - cycles / 2,
                perCycle);
    return perCycle;
}

} // namespace

// Each cycle loads the RC example (or resets it, every other cycle) and runs it to its end.
// ngspice 46 leaks about 1 KiB per load that nothing short of ending the process gives back
// (ADR 0002); this catches anything worse, such as plots no longer being freed.
TEST(load_and_reset_cycles_grow_memory_by_at_most_2_KiB_each)
{
    NgspiceEngine engine(shortRun());
    const double perCycle = growthPerCycle("load/reset", 100, 1000, [&](int k) {
        if (k % 2 == 0) {
            CHECK(engine.load(kRc));
        } else {
            engine.reset();
            CHECK(engine.error().empty());
        }
        CHECK(!engine.advance(1e-3).empty());
        CHECK(engine.error().find("stop time") != std::string::npos);
    });
    CHECK(perCycle < 2.0);
}

// Each cycle makes ngspice fail fatally, which frees and reloads the DLL, then runs the RC
// example on the fresh ngspice. Without ngSpice_Reset() before the DLL is freed, each reload
// would leave about 125 KiB behind.
TEST(fatal_error_reload_cycles_grow_memory_by_at_most_16_KiB_each)
{
    NgspiceEngine engine(shortRun());
    const double perCycle = growthPerCycle("fatal/reload", 20, 200, [&](int) {
        CHECK(!engine.load(kFatal));
        CHECK(engine.load(kRc));
        CHECK(!engine.advance(1e-3).empty());
    });
    CHECK(perCycle < 16.0);
}
