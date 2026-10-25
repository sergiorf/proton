// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/bench.h"
#include "proton/test/test.h"

#include <cmath>
#include <cstddef>
#include <numbers>
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
using proton::schematic::Diagnostic;
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

void feedAll(Bench& bench, const SimChunk& c)
{
    bench.feed(c, 0, c.time.size());
}

Bench::ScopeSettings scope(const std::string& name, std::string ch1, std::string ch2)
{
    Bench::ScopeSettings settings;
    settings.name = name;
    settings.config.timePerDiv = 100e-6;
    settings.config.mode = Scope::Mode::Auto;
    settings.channels = {std::move(ch1), std::move(ch2)};
    return settings;
}

// DC V with a 1 ms gate between `plus` and `com`.
Bench::MultimeterSettings meter(const std::string& name, std::string plus, std::string com = "0")
{
    Bench::MultimeterSettings settings;
    settings.name = name;
    settings.config.gateTime = 1e-3;
    settings.plus = std::move(plus);
    settings.com = std::move(com);
    return settings;
}

// scope1 with CH1 on `in` and CH2 on `out`, 100 us/div, Auto; dmm1 on `out`, DC V, 1 ms gate.
Bench::Config wired()
{
    Bench::Config config;
    config.scopes = {scope("scope1", "in", "out")};
    config.multimeters = {meter("dmm1", "out")};
    return config;
}

Bench attached(const Bench::Config& config, Circuit circuit = {})
{
    Bench bench(config);
    bench.contributeTo(circuit);
    bench.attach(kSignals);
    return bench;
}

} // namespace

TEST(the_engine_step_is_one_sample_of_the_fastest_scope)
{
    Scope::Config slow;
    slow.timePerDiv = 200e-6;
    CHECK_NEAR(proton::bench::maxStepFor(slow), 2e-6, 1e-18); // 2 ms over 1000 samples
    Bench::Config config;
    config.scopes = {scope("scope1", "in", "out"), scope("scope2", "in", "out")};
    config.scopes[0].config = slow;
    config.scopes[1].config.timePerDiv = 50e-6;
    CHECK_NEAR(Bench(config).maxStep(), 0.5e-6, 1e-18);
    CHECK_EQ(Bench().maxStep(), Bench::kMaxStepWithoutScope);
}

TEST(a_connected_generator_contributes_its_fragment_under_its_name)
{
    Bench::Config config;
    Bench::GeneratorSettings generator;
    generator.name = "gen2";
    generator.config.frequency = 2e3;
    generator.plus = "in";
    generator.com = "0";
    config.generators = {generator};
    Bench bench(config);
    Circuit circuit;
    circuit.components = {Component{ComponentKind::Resistor, "R1", {"in", "0"}, "1k"}};
    bench.contributeTo(circuit);
    CHECK_EQ(circuit.fragments.size(), 1u);
    CHECK_EQ(circuit.fragments[0].name, std::string("gen2"));
    CHECK(circuit.fragments[0].lines ==
          FunctionGenerator(generator.config).toSpice("gen2", "in", "0").lines);
}

TEST(a_generator_drives_between_its_terminals_whatever_com_is_on)
{
    Bench::Config config;
    Bench::GeneratorSettings generator;
    generator.plus = "in";
    generator.com = "mid"; // not ground
    config.generators = {generator};
    Bench bench(config);
    Circuit circuit;
    bench.contributeTo(circuit);
    CHECK_EQ(circuit.fragments.size(), 1u);
    CHECK(circuit.fragments[0].lines ==
          FunctionGenerator(generator.config).toSpice("gen1", "in", "mid").lines);
}

TEST(a_generator_with_an_open_terminal_contributes_nothing)
{
    Bench::Config config;
    Bench::GeneratorSettings generator;
    generator.plus = "in"; // COM open
    config.generators = {generator};
    Bench bench(config);
    Circuit circuit;
    bench.contributeTo(circuit);
    CHECK(circuit.fragments.empty());
}

TEST(probes_resolve_to_signals_ignoring_case)
{
    Bench::Config config = wired();
    config.scopes[0].channels = {"IN", "Out"};
    CHECK(attached(config).probeErrors().empty());
}

TEST(a_terminal_on_a_missing_net_is_reported_and_its_instrument_not_fed)
{
    Bench::Config config;
    config.scopes = {scope("scope1", "in", "mid")};
    config.multimeters = {meter("dmm1", "out", "nowhere"), meter("dmm2", "out")};
    Bench bench = attached(config);
    const std::vector<ProbeError>& errors = bench.probeErrors();
    CHECK_EQ(errors.size(), 2u);
    CHECK_EQ(errors[0].probe, std::string("scope1 CH2"));
    CHECK_EQ(errors[0].net, std::string("mid"));
    CHECK(errors[0].message.find("'mid'") != std::string::npos);
    CHECK_EQ(errors[1].probe, std::string("dmm1 COM"));
    CHECK_EQ(errors[1].net, std::string("nowhere"));

    feedAll(bench, chunk(0.0, 10e-3, 1e-6, 1.0, 2.0));
    CHECK(!bench.popFrame(0));   // scope1 has a missing probe
    CHECK(!bench.popReading(0)); // so has dmm1
    CHECK(bench.popReading(1));  // dmm2 is between out and ground
}

TEST(moving_a_terminal_attaches_it_again)
{
    Bench::Config config = wired();
    config.scopes[0].channels = {"in", "mid"};
    Bench bench = attached(config);
    CHECK_EQ(bench.probeErrors().size(), 1u);
    config.scopes[0].channels = {"in", "out"};
    bench.configure(config);
    CHECK(bench.probeErrors().empty());
    feedAll(bench, chunk(0.0, 10e-3, 1e-6, 1.0, 2.0));
    CHECK(bench.popFrame(0));
}

TEST(terminals_on_no_net_or_ground_read_zero)
{
    Bench::Config config;
    config.scopes = {scope("scope1", "", "gnd")};
    config.multimeters = {meter("dmm1", ""), meter("dmm2", "0"), meter("dmm3", "GND", "")};
    Circuit circuit;
    circuit.components = {Component{ComponentKind::Ground, "GND1", {"gnd"}, ""}};
    Bench bench = attached(config, circuit);
    CHECK(bench.probeErrors().empty());
    feedAll(bench, chunk(0.0, 10e-3, 1e-6, 1.0, 2.0));
    const std::optional<ScopeFrame> frame = bench.popFrame(0);
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

TEST(a_multimeter_reads_between_its_terminals)
{
    // dmm1 across two nets, neither of them ground: v(in) - v(out); dmm2 the other way round.
    Bench::Config config;
    config.multimeters = {meter("dmm1", "in", "out"), meter("dmm2", "out", "in")};
    Bench bench = attached(config);
    feedAll(bench, chunk(0.0, 1.5e-3, 1e-6, 3.0, 1.25));
    const std::optional<DmmReading> forward = bench.popReading(0);
    const std::optional<DmmReading> backward = bench.popReading(1);
    CHECK(forward && backward);
    if (forward && backward) {
        CHECK_NEAR(forward->value, 1.75, 1e-12);
        CHECK_NEAR(backward->value, -1.75, 1e-12);
    }
}

TEST(two_scopes_and_two_meters_are_fed_each_from_their_own_nets)
{
    Bench::Config config;
    config.scopes = {scope("scope1", "in", "out"), scope("scope2", "out", "")};
    config.multimeters = {meter("dmm1", "in"), meter("dmm2", "out")};
    Bench bench = attached(config);
    const SimChunk c = chunk(0.0, 10e-3, 1e-6, 1.0, 2.0);
    // In two parts: [begin, end) of the chunk.
    bench.feed(c, 0, 4000);
    bench.feed(c, 4000, c.time.size());
    const std::optional<ScopeFrame> first = bench.popFrame(0);
    const std::optional<ScopeFrame> second = bench.popFrame(1);
    CHECK(first && second);
    if (first && second) {
        CHECK_NEAR(first->values[0][500], 1.0, 1e-12);
        CHECK_NEAR(first->values[1][500], 2.0, 1e-12);
        CHECK_NEAR(second->values[0][500], 2.0, 1e-12);
        CHECK_NEAR(second->values[1][500], 0.0, 1e-12);
    }
    const std::optional<DmmReading> in = bench.popReading(0);
    const std::optional<DmmReading> out = bench.popReading(1);
    CHECK(in && out);
    if (in && out) {
        CHECK_NEAR(in->value, 1.0, 1e-12);
        CHECK_NEAR(out->value, 2.0, 1e-12);
    }
    CHECK(!bench.popFrame(2)); // no such instruments
    CHECK(!bench.popReading(2));
}

TEST(instrument_names_must_be_unique_and_not_a_designator)
{
    Bench::Config config;
    config.scopes = {scope("scope1", "in", "out")};
    config.multimeters = {meter("SCOPE1", "out"), meter("R1", "in"), meter("", "in")};
    Circuit circuit;
    circuit.components = {Component{ComponentKind::Resistor, "r1", {"in", "out"}, "1k"}};
    const std::vector<Diagnostic> diagnostics = Bench(config).check(circuit);
    CHECK_EQ(diagnostics.size(), 3u);
    for (const Diagnostic& diagnostic : diagnostics) {
        CHECK(diagnostic.severity == proton::schematic::Severity::Error);
    }
    if (diagnostics.size() == 3) {
        CHECK_EQ(diagnostics[0].element, std::string("SCOPE1")); // the second scope1
        CHECK_EQ(diagnostics[1].element, std::string("R1"));     // R1's designator
        CHECK(diagnostics[2].element.empty());                   // no name
    }
    CHECK(Bench(wired()).check(circuit).empty());
}

TEST(restart_starts_the_instruments_again_from_t_zero)
{
    Bench bench = attached(wired());
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
    Bench bench = attached(config);
    feedAll(bench, chunk(0.0, 0.5e-3, 1e-6, 1.0, 2.0));
    config.scopes[0].config.channels[0].voltsPerDiv = 2.0; // the scope only
    bench.configure(config);
    feedAll(bench, chunk(0.501e-3, 1.2e-3, 1e-6, 1.0, 2.0));
    // The multimeter's gate went on across the change.
    const std::optional<DmmReading> reading = bench.popReading(0);
    CHECK(reading.has_value());
    CHECK(reading && std::abs(reading->time - 1e-3) < 1e-12);
}

TEST(rearming_takes_another_single_frame)
{
    Bench::Config config = wired();
    config.scopes[0].config.mode = Scope::Mode::Single;
    Bench bench = attached(config);
    // A rising edge through 0 V on CH1 every millisecond.
    SimChunk c;
    c.signals.resize(2);
    for (int i = 0; i <= 20000; ++i) {
        const double t = static_cast<double>(i) * 1e-6;
        c.time.push_back(t);
        c.signals[0].push_back(std::sin(2.0 * std::numbers::pi * 1e3 * t));
        c.signals[1].push_back(0.0);
    }
    bench.feed(c, 0, 10000);
    CHECK(bench.popFrame(0).has_value());
    CHECK(bench.scopeStopped(0));
    bench.feed(c, 10000, 15000);
    CHECK(!bench.popFrame(0)); // stopped after its frame
    bench.rearmScope(0);
    CHECK(!bench.scopeStopped(0));
    bench.feed(c, 15000, c.time.size());
    CHECK(bench.popFrame(0).has_value());
}
