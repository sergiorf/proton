// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/sim/mock_engine.h"
#include "proton/test/test.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using proton::sim::indexOfSignal;
using proton::sim::MockEngine;
using proton::sim::SimChunk;
using Config = MockEngine::Config;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kPi = std::numbers::pi;
constexpr const char* kNetlist = "RC low-pass\nV1 in 0 SIN(0 1 1k)\nR1 in out 1k\nC1 out 0 100n\n";

// Runs `engine` for `count` chunks of `duration` and joins them into one.
SimChunk runFor(MockEngine& engine, double duration, int count)
{
    SimChunk all;
    all.signals.resize(engine.signalNames().size());
    for (int k = 0; k < count; ++k) {
        const SimChunk chunk = engine.advance(duration);
        all.time.insert(all.time.end(), chunk.time.begin(), chunk.time.end());
        for (std::size_t s = 0; s < chunk.signals.size(); ++s) {
            all.signals[s].insert(all.signals[s].end(), chunk.signals[s].begin(),
                                  chunk.signals[s].end());
        }
    }
    return all;
}

void load(MockEngine& engine)
{
    CHECK(engine.load(kNetlist));
}

Config withSteps(double minStep, double maxStep)
{
    Config config;
    config.minStep = minStep;
    config.maxStep = maxStep;
    return config;
}

// The RC low-pass driven by offset + A sin(wt) from its DC operating point at t = 0:
// v(out) = offset + A / (1 + k^2) (sin wt - k cos wt + k exp(-t / tau)), with k = w tau.
double analyticOutput(const Config& c, double t)
{
    const double tau = c.resistance * c.capacitance;
    const double w = 2.0 * kPi * c.frequency;
    const double k = w * tau;
    return c.offset + c.amplitude / (1.0 + k * k) *
                          (std::sin(w * t) - k * std::cos(w * t) + k * std::exp(-t / tau));
}

double maxOutputError(const Config& config, double duration)
{
    MockEngine engine(config);
    load(engine);
    const SimChunk run = runFor(engine, duration, 1);
    double worst = 0.0;
    for (std::size_t i = 0; i < run.time.size(); ++i) {
        worst = std::max(worst, std::fabs(run.signals[1][i] - analyticOutput(config, run.time[i])));
    }
    return worst;
}

} // namespace

TEST(nothing_is_loaded_at_first)
{
    MockEngine engine;
    CHECK(engine.signalNames().empty());
    CHECK(engine.error().empty());
    CHECK(engine.advance(1e-3).empty());
}

TEST(a_blank_netlist_fails_to_load)
{
    MockEngine engine;
    CHECK(!engine.load(" \n\t\r\n"));
    CHECK_EQ(engine.error(), std::string("empty netlist"));
    CHECK(engine.signalNames().empty());
    CHECK(engine.advance(1e-3).empty());

    // A failed load also unloads what was there, and a good one clears the error.
    CHECK(engine.load(kNetlist));
    CHECK(engine.error().empty());
    CHECK(!engine.load(""));
    CHECK(engine.advance(1e-3).empty());
}

TEST(signals_are_the_rc_input_and_output)
{
    MockEngine engine;
    load(engine);
    CHECK_EQ(engine.signalNames().size(), 2u);
    CHECK(indexOfSignal(engine.signalNames(), "v(in)") == 0u);
    CHECK(indexOfSignal(engine.signalNames(), "V(OUT)") == 1u);
}

TEST(invalid_durations_give_empty_chunks)
{
    MockEngine engine;
    load(engine);
    CHECK(engine.advance(0.0).empty());
    CHECK(engine.advance(-1e-3).empty());
    CHECK(engine.advance(kNaN).empty());
    CHECK(engine.advance(kInf).empty());
    // None of them moved time: the next chunk still starts at t = 0.
    const SimChunk chunk = engine.advance(1e-3);
    CHECK(!chunk.empty());
    CHECK_EQ(chunk.time.front(), 0.0);
}

TEST(time_is_strictly_increasing_irregular_and_continues_across_chunks)
{
    MockEngine engine;
    load(engine);
    const Config& c = engine.config();
    double previous = -1.0;
    double end = 0.0;
    double smallest = kInf;
    double largest = 0.0;
    for (int k = 0; k < 20; ++k) {
        const double start = end;
        const SimChunk chunk = engine.advance(1e-3);
        end += 1e-3;
        CHECK_EQ(chunk.signals.size(), 2u);
        CHECK_EQ(chunk.signals[0].size(), chunk.time.size());
        CHECK_EQ(chunk.signals[1].size(), chunk.time.size());
        CHECK(!chunk.empty());
        for (const double t : chunk.time) {
            CHECK(k == 0 ? t >= start : t > start); // t = 0 belongs to the first chunk
            CHECK(t <= end);
            if (previous >= 0.0) {
                const double step = t - previous;
                CHECK(step > 0.0);
                smallest = std::min(smallest, step);
                largest = std::max(largest, step);
            }
            previous = t;
        }
    }
    CHECK(smallest >= c.minStep * (1.0 - 1e-9));
    CHECK(largest <= c.maxStep * (1.0 + 1e-9));
    CHECK(largest - smallest > 0.5 * (c.maxStep - c.minStep)); // really uneven
}

TEST(input_is_the_sine)
{
    Config config;
    config.amplitude = 2.5;
    config.offset = 0.7;
    config.frequency = 1234.0;
    MockEngine engine(config);
    load(engine);
    const SimChunk run = runFor(engine, 1e-3, 5);
    CHECK(run.time.size() > 100u);
    for (std::size_t i = 0; i < run.time.size(); ++i) {
        CHECK_EQ(run.signals[0][i], 0.7 + 2.5 * std::sin(2.0 * kPi * 1234.0 * run.time[i]));
    }
}

TEST(rc_output_starts_at_the_dc_operating_point)
{
    Config config;
    config.offset = 1.5;
    MockEngine engine(config);
    load(engine);
    const SimChunk chunk = engine.advance(1e-3);
    CHECK_EQ(chunk.time.front(), 0.0);
    CHECK_EQ(chunk.signals[1].front(), 1.5);
}

TEST(rc_output_matches_the_analytic_response)
{
    // Includes the start-up transient: 5 ms is 50 time constants.
    Config config = withSteps(1e-7, 1e-6);
    config.amplitude = 2.0;
    config.offset = 0.5;
    config.frequency = 1e3;
    CHECK(maxOutputError(config, 5e-3) < 1e-5 * config.amplitude);
}

TEST(rc_output_error_shrinks_with_the_square_of_the_step)
{
    // Only the linearisation of the input across a step differs from the analytic answer.
    const double coarse = maxOutputError(withSteps(1e-6, 1e-5), 5e-3);
    const double fine = maxOutputError(withSteps(1e-7, 1e-6), 5e-3);
    CHECK(coarse < 1e-3);
    CHECK(fine < coarse / 50.0); // 100 for an exact h^2 law
}

TEST(rc_corner_is_minus_3_db)
{
    // At f = 1 / (2 pi R C) the steady-state output amplitude is A / sqrt(2).
    Config config = withSteps(1e-8, 1e-7);
    config.frequency = 1.0 / (2.0 * kPi * config.resistance * config.capacitance);
    MockEngine engine(config);
    load(engine);
    runFor(engine, 1e-3, 8); // 80 time constants: the transient is gone
    const SimChunk tail = runFor(engine, 1e-3, 2);
    double peak = 0.0;
    for (const double v : tail.signals[1]) {
        peak = std::max(peak, std::fabs(v));
    }
    CHECK_NEAR(20.0 * std::log10(peak / config.amplitude), -3.0103, 1e-3);
}

TEST(two_runs_are_bit_identical)
{
    MockEngine a;
    load(a);
    MockEngine b;
    load(b);
    for (int k = 0; k < 50; ++k) {
        const double duration = k % 3 == 0 ? 1e-3 : 0.37e-3;
        CHECK(a.advance(duration) == b.advance(duration));
    }
}

TEST(reset_and_load_start_the_same_run_again)
{
    MockEngine engine;
    load(engine);
    const SimChunk first = runFor(engine, 1e-3, 10);
    engine.reset();
    CHECK(runFor(engine, 1e-3, 10) == first);
    CHECK(engine.load(kNetlist));
    CHECK(runFor(engine, 1e-3, 10) == first);
}

TEST(the_run_does_not_depend_on_chunking)
{
    MockEngine whole;
    load(whole);
    MockEngine chunked;
    load(chunked);
    const SimChunk a = runFor(whole, 10e-3, 1);
    const SimChunk b = runFor(chunked, 0.1e-3, 110); // a little further, against rounding
    CHECK(b.time.size() >= a.time.size());
    const auto n = static_cast<std::ptrdiff_t>(a.time.size());
    CHECK(std::equal(a.time.begin(), a.time.end(), b.time.begin()));
    for (std::size_t s = 0; s < 2; ++s) {
        CHECK(std::equal(a.signals[s].begin(), a.signals[s].end(), b.signals[s].begin(),
                         b.signals[s].begin() + n));
    }
}

TEST(settings_are_sanitised)
{
    CHECK(MockEngine().config() == Config{});
    Config bad;
    bad.amplitude = kNaN;
    bad.offset = 1e9;
    bad.frequency = 0.0;
    bad.resistance = -5.0;
    bad.capacitance = kInf;
    bad.minStep = 1e-3;
    bad.maxStep = 1e-6; // below minStep
    const Config fixed = MockEngine::sanitise(bad);
    CHECK_EQ(fixed.amplitude, Config{}.amplitude);
    CHECK_EQ(fixed.offset, MockEngine::kMaxVoltage);
    CHECK_EQ(fixed.frequency, MockEngine::kMinFrequency);
    CHECK_EQ(fixed.resistance, MockEngine::kMinResistance);
    CHECK_EQ(fixed.capacitance, Config{}.capacitance);
    CHECK_EQ(fixed.minStep, 1e-3);
    CHECK_EQ(fixed.maxStep, 1e-3);
}
