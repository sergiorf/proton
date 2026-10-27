// SPDX-License-Identifier: AGPL-3.0-only
// RunControl on the mock engine, which ignores the circuit's elements and always gives v(in) and
// v(out) of its RC example. Wall-clock time is whatever the test passes in.
#include "proton/bench/run_control.h"
#include "proton/sim/mock_engine.h"
#include "proton/test/test.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>

using proton::bench::Bench;
using proton::bench::RunControl;
using proton::instruments::Multimeter;
using proton::instruments::Scope;
using proton::schematic::Circuit;
using proton::schematic::Component;
using proton::schematic::ComponentKind;
using proton::sim::ISimEngine;
using proton::sim::MockEngine;

namespace {

using Clock = std::chrono::steady_clock;

// Generous: sanitizer builds on CI runners are slow.
constexpr std::chrono::seconds kTimeout{20};

struct Factory
{
    int made = 0;          // engines made
    double lastStep = 0.0; // the step the last one was made for
    MockEngine::Config mock;
    std::string error; // when set, no engine is made

    RunControl::EngineFactory get()
    {
        return [this](double maxStep, std::string& why) -> std::unique_ptr<ISimEngine> {
            if (!error.empty()) {
                why = error;
                return nullptr;
            }
            ++made;
            lastStep = maxStep;
            return std::make_unique<MockEngine>(mock);
        };
    }
};

RunControl::Config unpaced()
{
    RunControl::Config config;
    config.realTime = false;
    return config;
}

// R1 and C1 between `in`, `out` and ground; the bench adds the generator.
Circuit rc(const std::string& resistance = "1k")
{
    Circuit circuit;
    circuit.components = {
        Component{ComponentKind::Resistor, "R1", {"in", "out"}, resistance},
        Component{ComponentKind::Capacitor, "C1", {"out", "0"}, "100n"},
    };
    return circuit;
}

// The generator on `in`, CH1 on `in`, CH2 on `out`, 200 us/div; a DMM on `out`, DC V, 10 ms gate.
Bench::Config wired()
{
    Bench::Config config;
    Bench::GeneratorSettings generator;
    generator.plus = "in";
    generator.com = "0";
    Bench::ScopeSettings scope;
    scope.config.timePerDiv = 200e-6;
    scope.config.mode = Scope::Mode::Auto;
    scope.channels = {"in", "out"};
    Bench::MultimeterSettings meter;
    meter.config.gateTime = 10e-3;
    meter.plus = "out";
    meter.com = "0";
    config.generators = {generator};
    config.scopes = {scope};
    config.multimeters = {meter};
    return config;
}

// Pumps at wall-clock time `wall` until `done`, or the timeout. Returns `done()`.
bool pumpUntil(RunControl& control, const std::function<bool()>& done, double wall = 0.0)
{
    const Clock::time_point deadline = Clock::now() + kTimeout;
    for (;;) {
        control.pump(wall);
        if (done()) {
            return true;
        }
        if (Clock::now() > deadline) {
            return false;
        }
        std::this_thread::yield();
    }
}

bool runTo(RunControl& control, double time)
{
    return pumpUntil(control, [&] {
        return control.fedThrough() >= time || control.state() != RunControl::State::Running;
    });
}

} // namespace

TEST(nothing_runs_before_start)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.pump(0.0);
    CHECK(control.state() == RunControl::State::Idle);
    CHECK_EQ(control.restarts(), 0u);
    CHECK_EQ(factory.made, 0);
}

TEST(a_run_feeds_the_bench)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.start();
    CHECK(runTo(control, 0.05));
    CHECK(control.state() == RunControl::State::Running);
    CHECK(control.error().empty());
    CHECK(control.diagnostics().empty());
    CHECK(control.netlist().find("Rgen1 gen1_int in 50") != std::string::npos);
    CHECK(control.probeErrors().empty());
    CHECK_EQ(control.restarts(), 1u);
    CHECK_EQ(factory.made, 1);
    CHECK_EQ(factory.lastStep, 2e-6); // one sample at 200 us/div
    CHECK(control.lastTime() >= 0.05 - 1e-5);
    CHECK(control.bench().popFrame(0).has_value());
    const std::optional<proton::instruments::DmmReading> reading = control.bench().popReading(0);
    CHECK(reading.has_value());
    CHECK(reading && std::abs(reading->value) < 0.05); // the mock's sine around 0 V
}

TEST(changes_between_pumps_restart_once)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.start();
    CHECK(runTo(control, 0.02));
    const double before = control.lastTime();

    // A circuit change and two generator knobs before the next pump: one restart.
    control.setCircuit(rc("2k"));
    Bench::Config config = control.bench().config();
    config.generators[0].config.frequency = 2e3;
    control.bench().configure(config);
    config.generators[0].config.amplitude = 0.5;
    control.bench().configure(config);
    control.pump(0.0);
    CHECK_EQ(control.restarts(), 2u);
    CHECK(control.state() == RunControl::State::Running);
    CHECK(control.netlist().find("R1 in out 2k") != std::string::npos);
    CHECK(control.netlist().find("SIN(0 500m 2k)") != std::string::npos);
    CHECK(control.lastTime() < before); // the new run starts again from t = 0
    CHECK_EQ(factory.made, 1);          // same step, same engine

    // Nothing changed: no restart.
    CHECK(runTo(control, 0.02));
    CHECK_EQ(control.restarts(), 2u);
}

TEST(knobs_that_keep_the_step_do_not_restart)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.start();
    CHECK(runTo(control, 0.01));
    Bench::Config config = control.bench().config();
    config.scopes[0].config.trigger.level = 0.3;
    config.scopes[0].config.channels[1].voltsPerDiv = 0.5;
    config.multimeters[0].config.mode = Multimeter::Mode::AcVoltage;
    config.scopes[0].channels = {"out", "in"};
    control.bench().configure(config);
    CHECK(runTo(control, 0.03));
    CHECK_EQ(control.restarts(), 1u);
    CHECK(control.state() == RunControl::State::Running);
}

TEST(a_timebase_that_changes_the_step_restarts_with_a_new_engine)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.start();
    CHECK(runTo(control, 0.01));
    Bench::Config config = control.bench().config();
    config.scopes[0].config.timePerDiv = 1e-3;
    control.bench().configure(config);
    control.pump(0.0);
    CHECK_EQ(control.restarts(), 2u);
    CHECK_EQ(factory.made, 2);
    CHECK_EQ(factory.lastStep, 1e-5);
    CHECK(runTo(control, 0.01));
    CHECK(control.state() == RunControl::State::Running);
}

TEST(an_invalid_circuit_does_not_run_until_it_is_fixed)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    control.bench().configure(wired());
    control.setCircuit(rc("0"));
    control.start();
    control.pump(0.0);
    CHECK(control.state() == RunControl::State::Invalid);
    CHECK(!control.diagnostics().empty());
    CHECK_EQ(control.error(), control.diagnostics().front().message);
    CHECK(control.netlist().empty());
    CHECK_EQ(factory.made, 0);
    control.pump(0.0);
    CHECK_EQ(control.restarts(), 1u); // no retry while nothing changes

    control.setCircuit(rc());
    CHECK(runTo(control, 0.01));
    CHECK(control.state() == RunControl::State::Running);
    CHECK(control.error().empty());
}

TEST(an_instrument_named_like_a_component_does_not_run)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    Bench::Config config = wired();
    config.multimeters[0].name = "R1";
    control.bench().configure(config);
    control.setCircuit(rc());
    control.start();
    control.pump(0.0);
    CHECK(control.state() == RunControl::State::Invalid);
    CHECK(control.error().find("R1") != std::string::npos);
    CHECK_EQ(factory.made, 0);
}

TEST(a_missing_net_is_reported_while_the_run_goes_on)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    Bench::Config config = wired();
    config.scopes[0].channels = {"in", "mid"};
    control.bench().configure(config);
    control.setCircuit(rc());
    control.start();
    CHECK(runTo(control, 0.02));
    CHECK(control.state() == RunControl::State::Running);
    CHECK_EQ(control.probeErrors().size(), 1u);
    CHECK(!control.probeErrors().empty() && control.probeErrors()[0].probe == "scope1 CH2");
    CHECK(!control.bench().popFrame(0)); // the scope has a missing probe
    CHECK(control.bench().popReading(0).has_value());

    // Moving the probe fixes it without a restart.
    config.scopes[0].channels = {"in", "out"};
    control.bench().configure(config);
    CHECK(control.probeErrors().empty());
    // On from wherever the run is: an unpaced pump may already have fed well past 0.02 s.
    CHECK(runTo(control, control.fedThrough() + 0.02));
    CHECK(control.bench().popFrame(0).has_value());
    CHECK_EQ(control.restarts(), 1u);
}

TEST(no_engine_fails_with_the_factory_s_reason)
{
    Factory factory;
    factory.error = "no simulator here";
    RunControl control(factory.get(), unpaced());
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.start();
    control.pump(0.0);
    control.pump(0.0);
    CHECK(control.state() == RunControl::State::Failed);
    CHECK_EQ(control.error(), std::string("no simulator here"));
    CHECK(!control.netlist().empty());
    CHECK_EQ(control.restarts(), 1u);
}

TEST(an_engine_failure_ends_the_run_after_what_came_before)
{
    Factory factory;
    factory.mock.failAt = 0.05;
    RunControl control(factory.get(), unpaced());
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.start();
    CHECK(pumpUntil(control, [&] { return control.state() != RunControl::State::Running; }));
    CHECK(control.state() == RunControl::State::Failed);
    CHECK_EQ(control.error(), std::string(MockEngine::kFailure));
    CHECK(control.lastTime() <= 0.05);
    CHECK(control.lastTime() > 0.049);
}

TEST(a_paced_run_feeds_only_what_is_due)
{
    Factory factory;
    RunControl control(factory.get()); // real time
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.start();
    control.pump(100.0); // the run starts at wall-clock time 100 s
    // Due is wall-clock time since the start, give or take the rounding of 100.02 - 100.
    CHECK(pumpUntil(control, [&] { return control.fedThrough() >= 0.02 - 1e-9; }, 100.02));
    CHECK_NEAR(control.fedThrough(), 0.02, 1e-9);
    CHECK(control.lastTime() <= 0.02 + 1e-9);
    CHECK(pumpUntil(control, [&] { return control.fedThrough() >= 0.03 - 1e-9; }, 100.03));
    CHECK(control.lastTime() <= 0.03 + 1e-9);
    CHECK_EQ(control.speed(), 1.0);
}

TEST(a_limit_holds_the_rest_back)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.start();
    const Clock::time_point deadline = Clock::now() + kTimeout;
    while (control.fedThrough() < 0.01 && Clock::now() < deadline) {
        control.pump(0.0, 0.01);
    }
    CHECK_EQ(control.fedThrough(), 0.01);
    CHECK(control.lastTime() <= 0.01);
}

TEST(pause_freezes_the_run_and_start_goes_on_from_there)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    control.bench().configure(wired()); // the DMM reads every 10 ms gate
    control.setCircuit(rc());
    control.start();
    const Clock::time_point deadline = Clock::now() + kTimeout;
    while (control.fedThrough() < 0.015 && Clock::now() < deadline) {
        control.pump(0.0, 0.015); // halfway through the second gate
    }
    CHECK_EQ(control.fedThrough(), 0.015);
    while (control.bench().popReading(0)) {
    }
    while (control.bench().popFrame(0)) {
    }

    control.pause();
    CHECK(control.state() == RunControl::State::Paused);
    const double lastTime = control.lastTime();
    for (int i = 0; i < 100; ++i) {
        control.pump(0.0);
    }
    CHECK_EQ(control.fedThrough(), 0.015); // simulation time stands still
    CHECK_EQ(control.lastTime(), lastTime);
    CHECK(!control.bench().popReading(0));
    CHECK(!control.bench().popFrame(0));

    control.start();
    CHECK(control.state() == RunControl::State::Running);
    while (control.fedThrough() < 0.025 && Clock::now() < deadline) {
        control.pump(0.0, 0.025);
    }
    CHECK_EQ(control.fedThrough(), 0.025);
    CHECK(control.lastTime() > lastTime); // on from the pause, not from t = 0
    CHECK_EQ(control.restarts(), 1u);
    CHECK_EQ(factory.made, 1);
    // The second gate, half counted before the pause, closed at 20 ms: the meter kept its state.
    CHECK(control.bench().popReading(0).has_value());
}

TEST(a_paced_run_does_not_catch_up_on_a_pause)
{
    Factory factory;
    RunControl control(factory.get()); // real time
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.start();
    control.pump(100.0);
    CHECK(pumpUntil(control, [&] { return control.fedThrough() >= 0.02 - 1e-9; }, 100.02));
    const double paused = control.fedThrough();
    control.pause();
    control.pump(150.0);
    CHECK_EQ(control.fedThrough(), paused);

    // A minute later on the wall clock, the run goes on from where it paused.
    control.start();
    control.pump(200.0);
    CHECK_NEAR(control.fedThrough(), paused, 1e-9);
    CHECK(pumpUntil(control, [&] { return control.fedThrough() >= paused + 0.01 - 1e-9; },
                    200.01));
    CHECK_NEAR(control.fedThrough(), paused + 0.01, 1e-9);
    CHECK_EQ(control.restarts(), 1u);
}

TEST(changes_while_paused_restart_on_start)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.start();
    CHECK(runTo(control, 0.02));
    control.pause();
    control.setCircuit(rc("2k"));
    control.pump(0.0);
    CHECK(control.state() == RunControl::State::Paused);
    CHECK_EQ(control.restarts(), 1u);
    const double before = control.lastTime();
    control.start();
    control.pump(0.0);
    CHECK(control.state() == RunControl::State::Running);
    CHECK_EQ(control.restarts(), 2u);
    CHECK(control.lastTime() < before);
}

TEST(stop_ends_a_paused_run_and_start_begins_again)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    control.pause(); // not running: nothing to pause
    CHECK(control.state() == RunControl::State::Idle);
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.start();
    CHECK(runTo(control, 0.02));
    const double before = control.lastTime();
    control.pause();
    control.stop();
    CHECK(control.state() == RunControl::State::Idle);
    control.start();
    control.pump(0.0);
    CHECK(control.state() == RunControl::State::Running);
    CHECK_EQ(control.restarts(), 2u);
    CHECK(control.lastTime() < before); // from t = 0
}

TEST(stop_ends_the_run_and_start_restarts_it)
{
    Factory factory;
    RunControl control(factory.get(), unpaced());
    control.bench().configure(wired());
    control.setCircuit(rc());
    control.start();
    CHECK(runTo(control, 0.01));
    control.stop();
    CHECK(control.state() == RunControl::State::Idle);
    control.pump(0.0);
    CHECK(control.state() == RunControl::State::Idle);
    control.start();
    CHECK(runTo(control, 0.01));
    CHECK_EQ(control.restarts(), 2u);
}
