// SPDX-License-Identifier: AGPL-3.0-only
// The examples, and the power supply, on ngspice through RunControl, as the app runs them, against
// the analytic answers.
// Its own executable: ngspice keeps process-wide state.
#include "proton/bench/engines.h"
#include "proton/core/spice_number.h"
#include "proton/project/project.h"
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

using proton::project::Example;
using proton::bench::RunControl;
using proton::instruments::DmmReading;
using proton::instruments::ScopeFrame;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::chrono::seconds kTimeout{60};

// The examples as the app loads them, in menu order.
std::vector<Example> examples()
{
    return proton::project::examples(PROTON_EXAMPLES_DIR);
}

// What multimeter `meter` and the first scope of `example` show by 0.25 s: the meter's reading
// at the end of its second gate (0.2 s), past the start-up transient, and the scope's last frame.
// Empty if the run failed or timed out.
struct Shown
{
    std::optional<DmmReading> reading;
    std::optional<ScopeFrame> frame;
};

std::optional<Shown> run(const Example& example, std::size_t meter = 0)
{
    RunControl::Config config;
    config.realTime = false;
    RunControl control(proton::bench::ngspiceEngines(), config);
    control.bench().configure(example.bench);
    control.setCircuit(example.circuit);
    control.start();
    const Clock::time_point deadline = Clock::now() + kTimeout;
    Shown shown;
    while (Clock::now() < deadline) {
        control.pump(0.0, 0.25);
        if (std::optional<DmmReading> r = control.bench().popReading(meter)) {
            shown.reading = r;
        }
        if (std::optional<ScopeFrame> f = control.bench().popFrame(0)) {
            shown.frame = std::move(f);
        }
        if (control.state() != RunControl::State::Running) {
            std::printf("%s: %s\n", example.name.c_str(), control.error().c_str());
            return std::nullopt;
        }
        if (control.fedThrough() >= 0.25) {
            return shown;
        }
        std::this_thread::yield();
    }
    return std::nullopt;
}

std::optional<DmmReading> secondReading(const Example& example, std::size_t meter = 0)
{
    const std::optional<Shown> shown = run(example, meter);
    return shown ? shown->reading : std::nullopt;
}

// The first time at or after the trigger where channel `channel` of `frame` rises through 0 V,
// relative to the trigger, by linear interpolation between samples.
std::optional<double> risingZero(const ScopeFrame& frame, std::size_t channel)
{
    const std::vector<double>& v = frame.values[channel];
    for (std::size_t i = 1; i < v.size(); ++i) {
        const double t = frame.startTime + static_cast<double>(i) * frame.sampleInterval;
        if (t >= 0.0 && v[i - 1] < 0.0 && v[i] >= 0.0) {
            return t - frame.sampleInterval * v[i] / (v[i] - v[i - 1]);
        }
    }
    return std::nullopt;
}

// The generator: 1 V peak at 1 kHz, ideal; R1 1k, C1 100n.
constexpr double kOmega = 2.0 * std::numbers::pi * 1e3;
constexpr double kR = 1e3;
constexpr double kC = 100e-9;
constexpr double kXc = 1.0 / (kOmega * kC); // capacitor's reactance, ohm

} // namespace

TEST(rc_low_pass_reads_the_attenuated_rms)
{
    const std::optional<DmmReading> reading = secondReading(examples()[0]);
    CHECK(reading.has_value());
    // Across C1 of the divider R1 and C1.
    const double gain = kXc / std::hypot(kR, kXc);
    CHECK(reading && std::abs(reading->value - gain / std::sqrt(2.0)) < 1e-3);
}

// The usability session (PLAN.md M4.8): with the generator set to 1 / (2 pi R C), the meter on
// `out` reads -3 dB of the generator's 1 V peak, 0.5000 V RMS, and on the scope, at 50 us/div,
// CH2 (`out`) rises through 0 V an eighth of a period after CH1 (`in`): a 45 degree lag.
TEST(rc_low_pass_at_its_corner_reads_minus_3_db_and_lags_45_degrees)
{
    Example example = examples()[0];
    const double corner = 1.0 / (2.0 * std::numbers::pi * kR * kC);
    example.bench.generators[0].config.frequency = corner;
    example.bench.scopes[0].config.timePerDiv = 50e-6;
    const std::optional<Shown> shown = run(example);
    CHECK(shown && shown->reading && shown->frame && shown->frame->triggered);
    if (!shown || !shown->reading || !shown->frame) {
        return;
    }
    const double decibels = 20.0 * std::log10(shown->reading->value * std::sqrt(2.0));
    CHECK_NEAR(decibels, -3.0103, 0.01);
    const std::optional<double> in = risingZero(*shown->frame, 0);
    const std::optional<double> out = risingZero(*shown->frame, 1);
    CHECK(in && out);
    if (in && out) {
        const double lag = (*out - *in) * corner * 360.0; // degrees
        CHECK_NEAR(lag, 45.0, 1.0);
    }
}

TEST(voltage_divider_reads_half_the_battery)
{
    const std::optional<DmmReading> reading = secondReading(examples()[1]);
    CHECK(reading.has_value());
    CHECK(reading && std::abs(reading->value - 2.5) < 1e-6);
}

// The M3 exit check: a second meter across R1, between `in` and `out`, neither of them ground.
TEST(a_meter_across_r1_reads_the_voltage_across_it)
{
    Example example = examples()[0];
    proton::bench::Bench::MultimeterSettings across;
    across.name = "dmm2";
    across.config.mode = proton::instruments::Multimeter::Mode::AcVoltage;
    across.plus = "in";
    across.com = "out";
    example.bench.multimeters.push_back(across);
    const std::optional<DmmReading> out = secondReading(example, 0);
    const std::optional<DmmReading> r1 = secondReading(example, 1);
    CHECK(out && r1);
    // R1's share of the divider R1 and C1.
    const double gainOut = kXc / std::hypot(kR, kXc);
    const double gainR1 = kR / std::hypot(kR, kXc);
    CHECK(out && std::abs(out->value - gainOut / std::sqrt(2.0)) < 1e-3);
    CHECK(r1 && std::abs(r1->value - gainR1 / std::sqrt(2.0)) < 1e-3);
}

// A generator whose COM is not ground: 2 V DC on top of a 1 V battery reads 3 V against ground.
TEST(a_generator_floats_on_its_com)
{
    Example example = examples()[1]; // the divider, which has no generator
    example.circuit.components = {
        {proton::schematic::ComponentKind::VoltageSource, "V1", {"mid", "0"}, "1"},
        {proton::schematic::ComponentKind::Resistor, "R1", {"in", "0"}, "1meg"},
        {proton::schematic::ComponentKind::Resistor, "R2", {"out", "0"}, "1k"},
        {proton::schematic::ComponentKind::Resistor, "R3", {"in", "out"}, "1"},
    };
    proton::bench::Bench::GeneratorSettings& generator = example.bench.generators.emplace_back();
    generator.config.waveform = proton::instruments::FunctionGenerator::Waveform::Dc;
    generator.config.offset = 2.0;
    generator.config.outputImpedance = 0.0;
    generator.plus = "in";
    generator.com = "mid";
    example.bench.multimeters[0].plus = "in";
    const std::optional<DmmReading> reading = secondReading(example);
    CHECK(reading && std::abs(reading->value - 3.0) < 1e-6);
}

TEST(rc_high_pass_reads_the_attenuated_rms)
{
    const std::optional<DmmReading> reading = secondReading(examples()[2]);
    CHECK(reading.has_value());
    // Across R1 of the divider C1 and R1.
    const double gain = kR / std::hypot(kR, kXc);
    CHECK(reading && std::abs(reading->value - gain / std::sqrt(2.0)) < 1e-3);
}

TEST(loaded_divider_reads_the_divider_of_r1_and_r2_parallel_r3)
{
    const std::optional<DmmReading> reading = secondReading(examples()[3]);
    const double lower = 10e3 * 10e3 / (10e3 + 10e3); // R2 || R3
    CHECK(reading && std::abs(reading->value - 5.0 * lower / (10e3 + lower)) < 1e-4);
}

// R1 1k and C1 1u (tau 1 ms) driven by a 100 Hz square from 0 to 2 V: on DC V the meter reads its
// mean, 1 V, and one time constant after a rising edge out has covered 63 % of the way from where
// the falling half left it.
TEST(rc_charging_rises_with_its_time_constant)
{
    const std::optional<Shown> shown = run(examples()[4]);
    CHECK(shown && shown->reading && shown->frame && shown->frame->triggered);
    if (!shown || !shown->reading || !shown->frame) {
        return;
    }
    CHECK(std::abs(shown->reading->value - 1.0) < 2e-3);
    constexpr double kTau = 1e-3;
    const double low = 2.0 * std::exp(-5.0) / (1.0 + std::exp(-5.0)); // after 5 tau of falling
    const double expected = 2.0 - (2.0 - low) * std::exp(-1.0);
    const ScopeFrame& frame = *shown->frame;
    const auto sample = static_cast<std::size_t>(
        std::lround((kTau - frame.startTime) / frame.sampleInterval));
    CHECK(sample < frame.values[1].size());
    if (sample < frame.values[1].size()) {
        CHECK_NEAR(frame.values[1][sample], expected, 0.01);
    }
}

// L1 10m, C1 100n and R1 100 in series, driven at 5.033 kHz, within 0.1 Hz of 1 / (2 pi
// sqrt(L C)): the reactances cancel, so out on R1 follows in and reads 1 / sqrt(2).
TEST(rlc_at_resonance_passes_the_input)
{
    const Example example = examples()[5];
    const double resonance = 1.0 / (2.0 * std::numbers::pi * std::sqrt(10e-3 * 100e-9));
    CHECK(std::abs(example.bench.generators[0].config.frequency - resonance) < 0.1);
    const std::optional<DmmReading> reading = secondReading(example);
    CHECK(reading && std::abs(reading->value - 1.0 / std::sqrt(2.0)) < 2e-3);
}

// The PLAN.md M5.1 check on ngspice: psu1's CH1 at 5 V across R1 shows V/R on its display, and
// with the limit below that current, the flag.
namespace {

std::optional<proton::instruments::SupplyReading> supplyReading(double ohms, double limit)
{
    using proton::schematic::ComponentKind;
    proton::schematic::Circuit circuit;
    circuit.components = {
        {ComponentKind::Resistor, "R1", {"vcc", "0"}, proton::core::formatSpiceNumber(ohms)},
        {ComponentKind::Ground, "", {"0"}, ""},
    };
    proton::bench::Bench::Config bench;
    proton::bench::Bench::SupplySettings& supply = bench.supplies.emplace_back();
    supply.config.channels[0].voltage = 5.0;
    supply.config.channels[0].currentLimit = limit;
    supply.channels[0] = {"vcc", "0"};

    RunControl::Config config;
    config.realTime = false;
    RunControl control(proton::bench::ngspiceEngines(), config);
    control.bench().configure(bench);
    control.setCircuit(circuit);
    control.start();
    const Clock::time_point deadline = Clock::now() + kTimeout;
    std::optional<proton::instruments::SupplyReading> reading;
    while (Clock::now() < deadline) {
        control.pump(0.0, 0.25);
        if (auto r = control.bench().popSupplyReading(0)) {
            reading = r;
        }
        if (control.state() != RunControl::State::Running) {
            std::printf("supply: %s\n", control.error().c_str());
            return std::nullopt;
        }
        if (control.fedThrough() >= 0.25) {
            return reading;
        }
        std::this_thread::yield();
    }
    return std::nullopt;
}

} // namespace

TEST(a_supply_into_a_resistor_shows_v_over_r)
{
    const auto reading = supplyReading(1e3, 1.0);
    CHECK(reading.has_value());
    if (!reading) {
        return;
    }
    CHECK_NEAR(reading->channels[0].voltage, 5.0, 1e-9);
    CHECK_NEAR(reading->channels[0].current, 5.0 / 1e3, 1e-9);
    CHECK_EQ(reading->channels[0].currentText, std::string("0.0050"));
    CHECK(!reading->channels[0].overLimit);
}

TEST(a_supply_beyond_its_limit_shows_the_flag)
{
    const auto reading = supplyReading(10.0, 0.1); // 0.5 A against a 0.1 A limit
    CHECK(reading && reading->channels[0].overLimit);
    CHECK(reading && std::abs(reading->channels[0].current - 0.5) < 1e-9);
}

// The PLAN.md M5.2 check: V1 5 V drives R1 1k, the meter in series, and R2 1k. dmm1 on DC A reads
// Ohm's law, 5 / 2k = 2.5 mA; dmm2 on DC V across the meter reads 0 V.
TEST(an_ammeter_in_series_reads_ohms_law_and_drops_nothing)
{
    using proton::schematic::ComponentKind;
    Example example = examples()[1]; // the divider, for its meter
    example.circuit.components = {
        {ComponentKind::VoltageSource, "V1", {"in", "0"}, "5"},
        {ComponentKind::Resistor, "R1", {"in", "a"}, "1k"},
        {ComponentKind::Resistor, "R2", {"b", "0"}, "1k"},
        {ComponentKind::Ground, "", {"0"}, ""},
    };
    example.bench.multimeters.resize(1);
    example.bench.multimeters[0].config.mode = proton::instruments::Multimeter::Mode::DcCurrent;
    example.bench.multimeters[0].plus = "a";
    example.bench.multimeters[0].com = "b";
    proton::bench::Bench::MultimeterSettings across;
    across.name = "dmm2";
    across.plus = "a";
    across.com = "b";
    example.bench.multimeters.push_back(across);
    const std::optional<DmmReading> current = secondReading(example, 0);
    const std::optional<DmmReading> drop = secondReading(example, 1);
    CHECK(current && std::abs(current->value - 2.5e-3) < 1e-9);
    CHECK(current && current->display.text == "2.500" && current->display.unit == "mA");
    CHECK(drop && std::abs(drop->value) < 1e-9);
}
