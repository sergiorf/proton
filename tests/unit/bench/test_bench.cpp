// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/bench.h"
#include "proton/test/test.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

using proton::bench::Bench;
using proton::bench::ProbeError;
using proton::instruments::DmmReading;
using proton::instruments::FunctionGenerator;
using proton::instruments::Multimeter;
using proton::instruments::Scope;
using proton::instruments::ScopeFrame;
using proton::schematic::Circuit;
using proton::schematic::Component;
using proton::schematic::ComponentKind;
using proton::sim::SimChunk;

namespace {

const std::vector<std::string> kSignals = {"v(in)", "v(out)"};

// Points every `step` seconds from `from` up to `to`, with v(in) and v(out) held at `in` and
// `out`.
SimChunk chunk(double from, double to, double step, double in, double out)
{
    SimChunk result;
    result.signals.resize(2);
    for (long long i = 0;; ++i) {
        const double t = from + static_cast<double>(i) * step;
        if (t > to + step / 2) {
            break;
        }
        result.time.push_back(t);
        result.signals[0].push_back(in);
        result.signals[1].push_back(out);
    }
    return result;
}

// DC V with a 1 ms gate.
Multimeter::Config gate1ms()
{
    Multimeter::Config config;
    config.gateTime = 1e-3;
    return config;
}

void feedAll(Bench& bench, const SimChunk& c)
{
    bench.feed(c, 0, c.time.size());
}

// CH1 on `in`, CH2 on `out`, 100 us/div, Auto; one DMM on `out`, DC V with a 1 ms gate.
Bench::Config wired()
{
    Bench::Config config;
    config.scope.timePerDiv = 100e-6;
    config.scope.mode = Scope::Mode::Auto;
    config.scopeNets = {"in", "out"};
    config.multimeters = {{gate1ms(), "out"}};
    return config;
}

} // namespace

TEST(the_engine_step_is_one_scope_sample)
{
    Scope::Config scope;
    scope.timePerDiv = 200e-6;
    CHECK_NEAR(proton::bench::maxStepFor(scope), 2e-6, 1e-18); // 2 ms over 1000 samples
    CHECK_EQ(proton::bench::maxStepFor(scope), Scope(scope).sampleInterval());
    Bench::Config config;
    config.scope = scope;
    CHECK_EQ(Bench(config).maxStep(), Scope(scope).sampleInterval());
}

TEST(a_connected_generator_contributes_its_fragment)
{
    Bench::Config config;
    config.generator.frequency = 2e3;
    config.generatorNet = "in";
    Bench bench(config);
    Circuit circuit;
    circuit.components = {Component{ComponentKind::Resistor, "R1", {"in", "0"}, "1k"}};
    bench.contributeTo(circuit);
    CHECK_EQ(circuit.fragments.size(), 1u);
    CHECK_EQ(circuit.fragments[0].name, std::string("gen1"));
    CHECK(circuit.fragments[0].lines ==
          FunctionGenerator(config.generator).toSpice("gen1", "in", "0").lines);
    CHECK_EQ(circuit.components.size(), 1u);
}

TEST(an_unconnected_generator_contributes_nothing)
{
    Bench bench;
    Circuit circuit;
    bench.contributeTo(circuit);
    CHECK(circuit.fragments.empty());
}

TEST(probes_resolve_to_signals_ignoring_case)
{
    Bench::Config config = wired();
    config.scopeNets = {"IN", "Out"};
    Bench bench(config);
    Circuit circuit;
    bench.contributeTo(circuit);
    CHECK(bench.attach(kSignals).empty());
}

TEST(a_probe_on_a_missing_net_is_reported_and_its_instrument_not_fed)
{
    Bench::Config config = wired();
    config.scopeNets = {"in", "mid"};
    config.multimeters = {{gate1ms(), "nowhere"},
                          {gate1ms(), "out"}};
    Bench bench(config);
    Circuit circuit;
    bench.contributeTo(circuit);
    const std::vector<ProbeError>& errors = bench.attach(kSignals);
    CHECK_EQ(errors.size(), 2u);
    CHECK_EQ(errors[0].probe, std::string("CH2"));
    CHECK_EQ(errors[0].net, std::string("mid"));
    CHECK(errors[0].message.find("'mid'") != std::string::npos);
    CHECK_EQ(errors[1].probe, std::string("DMM1"));
    CHECK_EQ(errors[1].net, std::string("nowhere"));

    feedAll(bench, chunk(0.0, 10e-3, 1e-6, 1.0, 2.0));
    CHECK(!bench.popFrame());      // the scope has a missing probe
    CHECK(!bench.popReading(0));   // so has DMM1
    CHECK(bench.popReading(1));    // DMM2 is on out
}

TEST(moving_a_probe_attaches_it_again)
{
    Bench::Config config = wired();
    config.scopeNets = {"in", "mid"};
    Bench bench(config);
    Circuit circuit;
    bench.contributeTo(circuit);
    CHECK_EQ(bench.attach(kSignals).size(), 1u);
    config.scopeNets = {"in", "out"};
    bench.configure(config);
    CHECK(bench.probeErrors().empty());
    feedAll(bench, chunk(0.0, 10e-3, 1e-6, 1.0, 2.0));
    CHECK(bench.popFrame());
}

TEST(probes_on_no_net_or_ground_read_zero)
{
    Bench::Config config = wired();
    config.scopeNets = {"", "gnd"};
    config.multimeters = {{gate1ms(), ""},
                          {gate1ms(), "0"},
                          {gate1ms(), "GND"}};
    Bench bench(config);
    Circuit circuit;
    circuit.components = {Component{ComponentKind::Ground, "GND1", {"gnd"}, ""}};
    bench.contributeTo(circuit);
    CHECK(bench.attach(kSignals).empty());
    feedAll(bench, chunk(0.0, 10e-3, 1e-6, 1.0, 2.0));
    const std::optional<ScopeFrame> frame = bench.popFrame();
    CHECK(frame.has_value());
    if (frame) {
        CHECK_EQ(frame->values[0].front(), 0.0);
        CHECK_EQ(frame->values[1].back(), 0.0);
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const std::optional<DmmReading> reading = bench.popReading(i);
        CHECK(reading.has_value());
        CHECK(reading && reading->value == 0.0);
    }
}

TEST(feed_routes_each_probe_to_its_signal)
{
    Bench::Config config = wired();
    config.multimeters.push_back({gate1ms(), "in"});
    Bench bench(config);
    Circuit circuit;
    bench.contributeTo(circuit);
    bench.attach(kSignals);
    const SimChunk c = chunk(0.0, 10e-3, 1e-6, 1.0, 2.0);
    // In two parts: [begin, end) of the chunk.
    bench.feed(c, 0, 4000);
    bench.feed(c, 4000, c.time.size());
    const std::optional<ScopeFrame> frame = bench.popFrame();
    CHECK(frame.has_value());
    if (frame) {
        CHECK_NEAR(frame->values[0][500], 1.0, 1e-12);
        CHECK_NEAR(frame->values[1][500], 2.0, 1e-12);
    }
    const std::optional<DmmReading> out = bench.popReading(0);
    const std::optional<DmmReading> in = bench.popReading(1);
    CHECK(out && in);
    if (out && in) {
        CHECK_NEAR(out->value, 2.0, 1e-12);
        CHECK_NEAR(in->value, 1.0, 1e-12);
    }
    CHECK(!bench.popReading(2)); // no such multimeter
}

TEST(restart_starts_the_instruments_again_from_t_zero)
{
    Bench bench(wired());
    Circuit circuit;
    bench.contributeTo(circuit);
    bench.attach(kSignals);
    feedAll(bench, chunk(0.0, 0.5e-3, 1e-6, 1.0, 2.0)); // half a gate
    bench.restart();
    // A new run from t = 0: without the restart these points would not move time forward.
    feedAll(bench, chunk(0.0, 1.5e-3, 1e-6, 1.0, 3.0));
    const std::optional<DmmReading> reading = bench.popReading(0);
    CHECK(reading.has_value());
    if (reading) {
        CHECK_NEAR(reading->time, 1e-3, 1e-12);
        CHECK_NEAR(reading->value, 3.0, 1e-12); // nothing of the first run is left
    }
}

TEST(configure_restarts_only_the_instruments_that_change)
{
    Bench::Config config = wired();
    Bench bench(config);
    Circuit circuit;
    bench.contributeTo(circuit);
    bench.attach(kSignals);
    feedAll(bench, chunk(0.0, 0.5e-3, 1e-6, 1.0, 2.0));
    config.scope.channels[0].voltsPerDiv = 2.0; // the scope only
    bench.configure(config);
    feedAll(bench, chunk(0.501e-3, 1.2e-3, 1e-6, 1.0, 2.0));
    // The multimeter's gate went on across the change.
    const std::optional<DmmReading> reading = bench.popReading(0);
    CHECK(reading.has_value());
    CHECK(reading && std::abs(reading->time - 1e-3) < 1e-12);
}
