// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/spike/pipeline.h"

#include "proton/bench/bench.h"
#include "proton/bench/run_control.h"
#include "proton/core/spice_number.h"
#include "proton/schematic/netlist.h"
#include "proton/sim/mock_engine.h"
#ifdef PROTON_HAVE_NGSPICE
#include "proton/sim/ngspice_engine.h"
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <thread>
#include <utility>

namespace proton::spike {

namespace {

using instruments::FunctionGenerator;
using instruments::Multimeter;
using instruments::Scope;
using instruments::ScopeFrame;
using schematic::Circuit;
using schematic::Component;
using schematic::ComponentKind;

constexpr const char* kGenerator = "gen1";
constexpr const char* kInputNet = "in";
constexpr const char* kOutputNet = "out";

// The shortest text that reads back as `value`; locale-free.
std::string number(double value)
{
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return {buffer.data(), result.ptr};
}

Multimeter::Config meter(Multimeter::Mode mode, double gateTime)
{
    Multimeter::Config config;
    config.mode = mode;
    config.gateTime = gateTime;
    return config;
}

// The bench for the example: gen1 between `in` and ground; scope1 with CH1 on `in` and CH2 on
// `out`; dmm1 (DC V) and dmm2 (AC V) between `out` and ground.
bench::Bench::Config benchConfig(const RcExample& example, const PipelineSettings& settings)
{
    const std::string ground(schematic::kGroundNet);
    bench::Bench::Config config;
    config.generators = {{kGenerator, example.generator, kInputNet, ground}};
    config.scopes = {{"scope1", settings.scope, {kInputNet, kOutputNet}}};
    config.multimeters = {
        {"dmm1", meter(Multimeter::Mode::DcVoltage, settings.gateTime), kOutputNet, ground},
        {"dmm2", meter(Multimeter::Mode::AcVoltage, settings.gateTime), kOutputNet, ground},
    };
    return config;
}

// R1 and C1, without the generator, which the bench adds.
Circuit components(const RcExample& example)
{
    Circuit circuit;
    circuit.components = {
        Component{ComponentKind::Resistor, "R1", {kInputNet, kOutputNet}, example.resistance},
        Component{ComponentKind::Capacitor,
                  "C1",
                  {kOutputNet, std::string(schematic::kGroundNet)},
                  example.capacitance},
    };
    return circuit;
}

// Pops everything the bench's instruments have given into `result`.
void collect(bench::Bench& bench, PipelineResult& result)
{
    if (std::optional<ScopeFrame> frame = bench.popFrame(0)) {
        result.frames.push_back(std::move(*frame));
    }
    if (std::optional<instruments::DmmReading> reading = bench.popReading(0)) {
        result.dcReadings.push_back(std::move(*reading));
    }
    if (std::optional<instruments::DmmReading> reading = bench.popReading(1)) {
        result.acReadings.push_back(std::move(*reading));
    }
}

// The engines `settings` choose; a factory that makes none says why.
bench::RunControl::EngineFactory engines(const RcExample& example,
                                         const PipelineSettings& settings)
{
    return [example, settings](double maxStep,
                               std::string& error) -> std::unique_ptr<sim::ISimEngine> {
        if (settings.engine == PipelineSettings::Engine::Ngspice) {
#ifdef PROTON_HAVE_NGSPICE
            sim::NgspiceEngine::Config config;
            // A chunk past the duration, so the chunk that reaches the duration is never cut
            // short by the end of the run.
            const double chunk = sim::SimWorker::sanitise(settings.worker).chunkDuration;
            config.stopTime = settings.duration + chunk;
            config.maxStep = maxStep;
            return std::make_unique<sim::NgspiceEngine>(config);
#else
            error = "this build has no ngspice";
            return nullptr;
#endif
        }

        // The mock engine simulates the same circuit, set up from the same settings; it picks
        // its own steps.
        const FunctionGenerator::Config generator = FunctionGenerator::sanitise(example.generator);
        if (generator.waveform != FunctionGenerator::Waveform::Sine) {
            error = "the mock engine simulates only a sine generator";
            return nullptr;
        }
        sim::MockEngine::Config mock;
        mock.amplitude = generator.amplitude;
        mock.offset = generator.offset;
        mock.frequency = generator.frequency;
        // validate() passed, so both values parse.
        mock.resistance = core::parseSpiceNumber(example.resistance).value_or(mock.resistance);
        mock.capacitance = core::parseSpiceNumber(example.capacitance).value_or(mock.capacitance);
        mock.failAt = settings.failAt;
        return std::make_unique<sim::MockEngine>(mock);
    };
}

} // namespace

std::optional<PipelineSettings::Engine> parseEngine(std::string_view name)
{
    if (name == "mock") {
        return PipelineSettings::Engine::Mock;
    }
    if (name == "ngspice") {
        return PipelineSettings::Engine::Ngspice;
    }
    return std::nullopt;
}

PipelineSettings defaultSettings(const RcExample& example)
{
    PipelineSettings settings;
    settings.scope.timePerDiv = 200e-6;
    settings.scope.mode = Scope::Mode::Normal;
    settings.scope.trigger.source = Scope::TriggerSource::Channel1;
    settings.scope.trigger.slope = Scope::Slope::Rising;
    settings.scope.trigger.level = FunctionGenerator::sanitise(example.generator).offset;
    return settings;
}

Circuit buildCircuit(const RcExample& example)
{
    Circuit circuit = components(example);
    bench::Bench bench(benchConfig(example, defaultSettings(example)));
    bench.contributeTo(circuit);
    return circuit;
}

PipelineResult run(const RcExample& example, const PipelineSettings& settings)
{
    bench::RunControl control(engines(example, settings), {settings.worker, settings.realTime});
    control.bench().configure(benchConfig(example, settings));
    control.setCircuit(components(example));
    control.start();

    const auto clockStart = std::chrono::steady_clock::now();
    const auto wallNow = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - clockStart).count();
    };
    // Each pump feeds at most a chunk's worth of simulation time, and what the instruments gave is
    // popped after it.
    const double chunk = sim::SimWorker::sanitise(settings.worker).chunkDuration;
    double target = std::min(settings.duration, chunk);
    PipelineResult result;
    for (;;) {
        control.pump(wallNow(), target);
        collect(control.bench(), result);
        if (control.state() != bench::RunControl::State::Running) {
            result.error = control.error();
            break;
        }
        if (!control.probeErrors().empty()) {
            result.error = control.probeErrors().front().message;
            break;
        }
        if (control.fedThrough() >= settings.duration) {
            break;
        }
        if (control.fedThrough() >= target) {
            target = std::min(settings.duration, target + chunk);
        } else if (settings.realTime) {
            // The rest is not due yet.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {
            std::this_thread::yield();
        }
    }
    result.diagnostics = control.diagnostics();
    result.netlist = control.netlist();
    result.lastTime = control.lastTime();
    result.speed = control.speed();
    return result;
}

std::string frameCsv(const ScopeFrame& frame)
{
    std::string csv = "t_rel_s,ch1_v_in,ch2_v_out\n";
    const std::size_t count = frame.values[0].size();
    for (std::size_t i = 0; i < count; ++i) {
        const double t = frame.startTime + static_cast<double>(i) * frame.sampleInterval;
        csv += number(t);
        csv += ',';
        csv += number(frame.values[0][i]);
        csv += ',';
        csv += number(frame.values[1][i]);
        csv += '\n';
    }
    return csv;
}

std::string report(const RcExample& example, const PipelineSettings& settings,
                   const PipelineResult& result)
{
    const FunctionGenerator::Config generator = FunctionGenerator::sanitise(example.generator);
    std::ostringstream text;
    text << "# proton-spike: RC low-pass (R1 " << example.resistance << ", C1 "
         << example.capacitance << ") driven by " << kGenerator << ", sine "
         << number(generator.frequency) << " Hz, " << number(generator.amplitude) << " V around "
         << number(generator.offset) << " V; "
         << (settings.engine == PipelineSettings::Engine::Ngspice ? "ngspice" : "mock engine")
         << ", " << number(settings.duration) << " s" << (settings.realTime ? " in real time" : "")
         << '\n';
    if (!result.netlist.empty()) {
        text << "# netlist:\n";
        std::istringstream lines(result.netlist);
        for (std::string line; std::getline(lines, line);) {
            text << "#   " << line << '\n';
        }
    }
    if (!result.ok()) {
        text << "# error: " << result.error << '\n';
        return text.str();
    }
    const auto reading = [&](const char* name, const std::vector<instruments::DmmReading>& all) {
        text << "# dmm v(out) " << name << ": ";
        if (all.empty()) {
            text << "no reading\n";
        } else {
            text << all.back().display.text << ' ' << all.back().display.unit << '\n';
        }
    };
    reading("DC V", result.dcReadings);
    reading("AC V", result.acReadings);
    if (settings.realTime) {
        text << "# simulation speed: " << number(result.speed) << " x real time\n";
    }
    if (result.frames.empty()) {
        text << "# scope: no frame\n";
        return text.str();
    }
    const ScopeFrame& frame = result.frames.back();
    text << "# scope: CH1 v(in), CH2 v(out), " << number(settings.scope.timePerDiv) << " s/div; "
         << (frame.triggered ? "triggered" : "untriggered") << " by CH1 rising at "
         << number(settings.scope.trigger.level) << " V at t = " << number(frame.triggerTime)
         << " s; " << frame.values[0].size() << " samples; frame " << result.frames.size() << '\n';
    text << frameCsv(frame);
    return text.str();
}

} // namespace proton::spike
