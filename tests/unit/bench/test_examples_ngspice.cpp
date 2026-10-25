// SPDX-License-Identifier: AGPL-3.0-only
// The examples on ngspice through RunControl, as the app runs them, against the analytic answers.
// Its own executable: ngspice keeps process-wide state.
#include "proton/bench/engines.h"
#include "proton/bench/examples.h"
#include "proton/bench/run_control.h"
#include "proton/test/test.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using proton::bench::Example;
using proton::bench::RunControl;
using proton::instruments::DmmReading;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::chrono::seconds kTimeout{60};

// The multimeter's reading of `example` at the end of its second gate (0.2 s), past the start-up
// transient. Empty if the run failed or timed out.
std::optional<DmmReading> secondReading(const Example& example)
{
    RunControl::Config config;
    config.realTime = false;
    RunControl control(proton::bench::ngspiceEngines(), config);
    control.bench().configure(example.bench);
    control.setCircuit(example.circuit);
    control.start();
    const Clock::time_point deadline = Clock::now() + kTimeout;
    std::optional<DmmReading> reading;
    while (Clock::now() < deadline) {
        control.pump(0.0, 0.25);
        if (std::optional<DmmReading> r = control.bench().popReading(0)) {
            reading = r;
        }
        if (control.state() != RunControl::State::Running) {
            std::printf("%s: %s\n", example.name.c_str(), control.error().c_str());
            return std::nullopt;
        }
        if (control.fedThrough() >= 0.25) {
            return reading;
        }
        std::this_thread::yield();
    }
    return std::nullopt;
}

// The generator: 1 V peak at 1 kHz behind 50 ohm; R1 1k, C1 100n.
constexpr double kOmega = 2.0 * std::numbers::pi * 1e3;
constexpr double kSource = 50.0;
constexpr double kR = 1e3;
constexpr double kXc = 1.0 / (kOmega * 100e-9); // capacitor's reactance, ohm

} // namespace

TEST(rc_low_pass_reads_the_attenuated_rms)
{
    const std::optional<DmmReading> reading = secondReading(proton::bench::examples()[0]);
    CHECK(reading.has_value());
    // Across C1 of the divider R1 + 50 ohm and C1.
    const double gain = kXc / std::hypot(kR + kSource, kXc);
    CHECK(reading && std::abs(reading->value - gain / std::sqrt(2.0)) < 1e-3);
}

TEST(voltage_divider_reads_half_the_battery)
{
    const std::optional<DmmReading> reading = secondReading(proton::bench::examples()[1]);
    CHECK(reading.has_value());
    CHECK(reading && std::abs(reading->value - 2.5) < 1e-6);
}

TEST(rc_high_pass_reads_the_attenuated_rms)
{
    const std::optional<DmmReading> reading = secondReading(proton::bench::examples()[2]);
    CHECK(reading.has_value());
    // Across R1 of the divider C1 and R1 + 50 ohm.
    const double gain = kR / std::hypot(kR + kSource, kXc);
    CHECK(reading && std::abs(reading->value - gain / std::sqrt(2.0)) < 1e-3);
}
