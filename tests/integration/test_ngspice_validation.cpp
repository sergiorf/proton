// SPDX-License-Identifier: AGPL-3.0-only
// ngspice against analytic answers (M1.3): RC step response, RC low-pass at its corner, a voltage
// divider, and a function generator driving a load through its output impedance. Windows only:
// built when CMake found third_party/ngspice. Its own executable: ngspice keeps process-wide state.
//
// Each check states its tolerance below. The circuits are linear, so the only errors are
// ngspice's time discretisation (bounded by maxStep and its truncation-error control) and the
// trapezoidal integrals of the measurement.
#include "proton/instruments/function_generator.h"
#include "proton/schematic/circuit.h"
#include "proton/schematic/netlist.h"
#include "proton/schematic/validate.h"
#include "proton/sim/ngspice_engine.h"
#include "proton/test/test.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using proton::instruments::FunctionGenerator;
using proton::schematic::Circuit;
using proton::schematic::Component;
using proton::schematic::ComponentKind;
using proton::sim::indexOfSignal;
using proton::sim::NgspiceEngine;
using proton::sim::SimChunk;

namespace {

constexpr double kPi = std::numbers::pi;

// Stated tolerances. Measured with ngspice 46 (in brackets), the time-dependent ones with a
// margin of about ten. Resistive circuits are exact to rounding.
constexpr double kStepTolerance = 3e-5;              // V, pointwise, on a 1 V step [3.0e-6]
constexpr double kTauTolerance = 3e-5;               // relative, read off the step [2.7e-6]
constexpr double kCornerGainTolerance = 2e-4;        // dB [1.4e-5]
constexpr double kCornerPhaseTolerance = 1e-3;       // degrees [9.4e-5]
constexpr double kDividerTolerance = 1e-12;          // relative [4e-19 A on 2.5 mA]
constexpr double kGeneratorTolerance = 1e-12;        // V, pointwise [3.6e-15]
constexpr double kGeneratorCurrentTolerance = 1e-15; // A, pointwise [3.5e-18]

constexpr double kChunk = 1e-3; // s

// A whole run to `stopTime`, joined into one chunk, with the engine's signal names.
struct Run
{
    std::vector<std::string> names;
    SimChunk all;
    std::string error; // the engine's last message: "... reached its stop time ..." on success

    const std::vector<double>& signal(const char* name) const
    {
        static const std::vector<double> none;
        const std::optional<std::size_t> i = indexOfSignal(names, name);
        CHECK(i.has_value());
        return i ? all.signals[*i] : none;
    }
};

Run simulate(const std::string& netlist, double stopTime, double maxStep)
{
    NgspiceEngine::Config config;
    config.stopTime = stopTime;
    config.maxStep = maxStep;
    NgspiceEngine engine(config);
    Run run;
    CHECK(engine.load(netlist));
    CHECK_EQ(engine.error(), std::string());
    run.names = engine.signalNames();
    while (engine.error().empty()) {
        const SimChunk c = engine.advance(kChunk);
        run.all.time.insert(run.all.time.end(), c.time.begin(), c.time.end());
        run.all.signals.resize(c.signals.size());
        for (std::size_t s = 0; s < c.signals.size(); ++s) {
            run.all.signals[s].insert(run.all.signals[s].end(), c.signals[s].begin(),
                                      c.signals[s].end());
        }
    }
    run.error = engine.error();
    CHECK(run.error.find("stop time") != std::string::npos);
    CHECK(!run.all.empty());
    if (!run.all.empty()) {
        CHECK_EQ(run.all.time.front(), 0.0);
        CHECK_NEAR(run.all.time.back(), stopTime, 1e-12);
    }
    return run;
}

// The netlist of a circuit that validate() passes.
std::string netlistOf(const Circuit& circuit)
{
    CHECK(!proton::schematic::hasErrors(proton::schematic::validate(circuit)));
    return proton::schematic::buildNetlist(circuit);
}

Component part(ComponentKind kind, const char* designator, const char* a, const char* b,
               const char* value)
{
    return Component{kind, designator, {a, b}, value};
}

// `v` at time `t`, linear between the points around it.
double at(const std::vector<double>& time, const std::vector<double>& v, double t)
{
    const auto hi = std::lower_bound(time.begin(), time.end(), t);
    if (hi == time.begin()) {
        return v.front();
    }
    if (hi == time.end()) {
        return v.back();
    }
    const auto i = static_cast<std::size_t>(hi - time.begin());
    const double f = (t - time[i - 1]) / (time[i] - time[i - 1]);
    return v[i - 1] + f * (v[i] - v[i - 1]);
}

// The integral of `f(t) * v(t)` from t0 to t1 by the trapezoidal rule on the simulation's own
// points, plus both ends interpolated.
template <typename F>
double integral(const std::vector<double>& time, const std::vector<double>& v, double t0,
                double t1, F f)
{
    std::vector<double> t{t0};
    std::vector<double> y{f(t0) * at(time, v, t0)};
    for (std::size_t i = 0; i < time.size(); ++i) {
        if (time[i] > t0 && time[i] < t1) {
            t.push_back(time[i]);
            y.push_back(f(time[i]) * v[i]);
        }
    }
    t.push_back(t1);
    y.push_back(f(t1) * at(time, v, t1));
    double sum = 0.0;
    for (std::size_t i = 1; i < t.size(); ++i) {
        sum += 0.5 * (y[i] + y[i - 1]) * (t[i] - t[i - 1]);
    }
    return sum;
}

// The component of `v` at angular frequency `omega` over [t0, t1], a whole number of periods:
// v ~ amplitude * sin(omega t + phase).
struct Phasor
{
    double amplitude = 0.0;
    double phase = 0.0; // rad
};

Phasor phasor(const std::vector<double>& time, const std::vector<double>& v, double omega,
              double t0, double t1)
{
    const double scale = 2.0 / (t1 - t0);
    const auto sine = [&](double t) { return std::sin(omega * t); };
    const auto cosine = [&](double t) { return std::cos(omega * t); };
    const double s = scale * integral(time, v, t0, t1, sine);
    const double c = scale * integral(time, v, t0, t1, cosine);
    return {std::hypot(s, c), std::atan2(c, s)};
}

} // namespace

TEST(rc_step_response_follows_the_time_constant)
{
    // 1 V step into R = 1k, C = 1u: v(out) = 1 - exp(-t / tau), tau = 1 ms. The rise takes 1 ns,
    // so the step is at t = 0.5 ns to within a millionth of tau.
    constexpr double kTau = 1e-3;
    const Run run = simulate("* RC step\n"
                             "C1 out 0 1u\n"
                             "R1 in out 1k\n"
                             "V1 in 0 PULSE(0 1 0 1n 1n 1 2)\n",
                             5 * kTau, kTau / 100);
    const std::vector<double>& out = run.signal("v(out)");
    const std::vector<double>& time = run.all.time;
    if (out.size() != time.size()) {
        return;
    }
    double worst = 0.0;
    for (std::size_t i = 0; i < time.size(); ++i) {
        const double expected = time[i] < 0.5e-9 ? 0.0 : 1.0 - std::exp(-(time[i] - 0.5e-9) / kTau);
        worst = std::max(worst, std::fabs(out[i] - expected));
    }
    CHECK_NEAR(worst, 0.0, kStepTolerance);

    // tau is where v(out) crosses 1 - 1/e, interpolated between the points around it.
    const double level = 1.0 - std::exp(-1.0);
    const auto above = std::find_if(out.begin(), out.end(), [&](double v) { return v >= level; });
    CHECK(above != out.begin() && above != out.end());
    if (above != out.begin() && above != out.end()) {
        const auto i = static_cast<std::size_t>(above - out.begin());
        const double f = (level - out[i - 1]) / (out[i] - out[i - 1]);
        const double tau = time[i - 1] + f * (time[i] - time[i - 1]) - 0.5e-9;
        CHECK_NEAR(tau / kTau, 1.0, kTauTolerance);
    }
    CHECK_NEAR(out.back(), 1.0 - std::exp(-5.0), kStepTolerance);
}

TEST(rc_low_pass_at_its_corner_is_minus_3_db_and_minus_45_degrees)
{
    // R = 1k, C = 100n: f_c = 1 / (2 pi R C) = 1591.5 Hz. An ideal generator sine of 1 V at f_c.
    // After the start-up transient (tau = 100 us) has died out, v(out) / v(in) = 1 / (1 + j).
    constexpr double kTau = 1e3 * 100e-9;
    FunctionGenerator generator({
        .waveform = FunctionGenerator::Waveform::Sine,
        .frequency = 1.0 / (2.0 * kPi * kTau),
        .amplitude = 1.0,
        .offset = 0.0,
        .outputImpedance = 0.0,
    });
    Circuit circuit;
    circuit.components = {part(ComponentKind::Resistor, "R1", "in", "out", "1k"),
                          part(ComponentKind::Capacitor, "C1", "out", "0", "100n")};
    circuit.fragments = {{"gen1", generator.toSpice("gen1", "in", "0").lines}};

    const double omega = 2.0 * kPi * generator.config().frequency;
    const double period = 2.0 * kPi / omega;
    const Run run = simulate(netlistOf(circuit), 20 * period, period / 1000);
    // Periods 10 to 20: the transient is down by exp(-10 period / tau) = e^-63 there.
    const double t0 = 10 * period;
    const double t1 = 20 * period;
    const Phasor in = phasor(run.all.time, run.signal("v(in)"), omega, t0, t1);
    const Phasor out = phasor(run.all.time, run.signal("v(out)"), omega, t0, t1);
    CHECK_NEAR(in.amplitude, 1.0, 1e-6);
    CHECK_NEAR(in.phase, 0.0, 1e-6);
    const double gainDb = 20.0 * std::log10(out.amplitude / in.amplitude);
    const double phaseDeg = (out.phase - in.phase) * 180.0 / kPi;
    CHECK_NEAR(gainDb, -10.0 * std::log10(2.0), kCornerGainTolerance); // -3.0103 dB
    CHECK_NEAR(phaseDeg, -45.0, kCornerPhaseTolerance);
}

TEST(voltage_divider_matches_ohms_law)
{
    // 10 V across 1k over 3k: v(out) = 7.5 V; 2.5 mA leaves V1's positive terminal, which SPICE
    // reports as i(v1) = -2.5 mA.
    Circuit circuit;
    circuit.components = {part(ComponentKind::VoltageSource, "V1", "in", "0", "10"),
                          part(ComponentKind::Resistor, "R1", "in", "out", "1k"),
                          part(ComponentKind::Resistor, "R2", "out", "0", "3k")};
    const Run run = simulate(netlistOf(circuit), 1e-3, 1e-4);
    for (const double v : run.signal("v(in)")) {
        CHECK_NEAR(v, 10.0, 10.0 * kDividerTolerance);
    }
    for (const double v : run.signal("v(out)")) {
        CHECK_NEAR(v, 7.5, 7.5 * kDividerTolerance);
    }
    for (const double i : run.signal("i(v1)")) {
        CHECK_NEAR(i, -2.5e-3, 2.5e-3 * kDividerTolerance);
    }
}

TEST(generator_drives_a_load_through_its_output_impedance)
{
    // A sine of 2 V around 1 V behind 50 ohm into a 50 ohm load: the load sees half of it.
    {
        FunctionGenerator generator({
            .waveform = FunctionGenerator::Waveform::Sine,
            .frequency = 1e3,
            .amplitude = 2.0,
            .offset = 1.0,
            .outputImpedance = 50.0,
        });
        Circuit circuit;
        circuit.components = {part(ComponentKind::Resistor, "RL", "out", "0", "50")};
        circuit.fragments = {{"gen1", generator.toSpice("gen1", "out", "0").lines}};
        const Run run = simulate(netlistOf(circuit), 3e-3, 1e-6);
        const std::vector<double>& out = run.signal("v(out)");
        const std::vector<double>& current = run.signal("i(vgen1)");
        if (out.size() == run.all.time.size() && current.size() == run.all.time.size()) {
            for (std::size_t i = 0; i < out.size(); ++i) {
                const double open = 1.0 + 2.0 * std::sin(2.0 * kPi * 1e3 * run.all.time[i]);
                CHECK_NEAR(out[i], open * 50.0 / (50.0 + 50.0), kGeneratorTolerance);
                // The load current leaves the source's positive terminal.
                CHECK_NEAR(current[i], -out[i] / 50.0, kGeneratorCurrentTolerance);
            }
        }
    }
    // DC 3.3 V behind 50 ohm into 150 ohm: 3.3 * 150 / 200 = 2.475 V.
    {
        FunctionGenerator generator({
            .waveform = FunctionGenerator::Waveform::Dc,
            .offset = 3.3,
            .outputImpedance = 50.0,
        });
        Circuit circuit;
        circuit.components = {part(ComponentKind::Resistor, "RL", "out", "0", "150")};
        circuit.fragments = {{"gen1", generator.toSpice("gen1", "out", "0").lines}};
        const Run run = simulate(netlistOf(circuit), 1e-3, 1e-4);
        for (const double v : run.signal("v(out)")) {
            CHECK_NEAR(v, 2.475, kGeneratorTolerance);
        }
        for (const double i : run.signal("i(vgen1)")) {
            CHECK_NEAR(i, -2.475 / 150.0, kGeneratorCurrentTolerance);
        }
    }
}
