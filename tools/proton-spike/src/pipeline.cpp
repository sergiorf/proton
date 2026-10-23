// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/spike/pipeline.h"

#include "proton/core/spice_number.h"
#include "proton/schematic/netlist.h"
#include "proton/sim/mock_engine.h"
#ifdef PROTON_HAVE_NGSPICE
#include "proton/sim/ngspice_engine.h"
#endif

#include <algorithm>
#include <array>
#include <charconv>
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

// Feeds simulation output to the instruments and collects what they give.
class Pump
{
public:
    Pump(const PipelineSettings& settings, PipelineResult& result)
        : scope_(settings.scope), dc_(meter(Multimeter::Mode::DcVoltage, settings.gateTime)),
          ac_(meter(Multimeter::Mode::AcVoltage, settings.gateTime)), duration_(settings.duration),
          result_(result)
    {}

    // Feeds the points of `chunk` up to the duration. Returns true once the duration is reached.
    bool feed(const sim::SimChunk& chunk, std::size_t in, std::size_t out)
    {
        const auto end = std::upper_bound(chunk.time.begin(), chunk.time.end(), duration_);
        const auto count = static_cast<std::size_t>(end - chunk.time.begin());
        const std::span<const double> time(chunk.time.data(), count);
        const std::span<const double> vin(chunk.signals[in].data(), count);
        const std::span<const double> vout(chunk.signals[out].data(), count);
        scope_.feed(time, vin, vout);
        dc_.feed(time, vout);
        ac_.feed(time, vout);
        if (std::optional<ScopeFrame> frame = scope_.popFrame()) {
            result_.frames.push_back(std::move(*frame));
        }
        if (std::optional<instruments::DmmReading> reading = dc_.popReading()) {
            result_.dcReadings.push_back(std::move(*reading));
        }
        if (std::optional<instruments::DmmReading> reading = ac_.popReading()) {
            result_.acReadings.push_back(std::move(*reading));
        }
        if (count > 0) {
            result_.lastTime = time.back();
        }
        return !chunk.time.empty() && chunk.time.back() >= duration_;
    }

private:
    Scope scope_;
    Multimeter dc_;
    Multimeter ac_;
    double duration_;
    PipelineResult& result_;
};

// The engine `settings` choose, or nullptr with `error` saying why there is none.
std::unique_ptr<sim::ISimEngine> makeEngine(const RcExample& example,
                                            const PipelineSettings& settings, std::string& error)
{
    if (settings.engine == PipelineSettings::Engine::Ngspice) {
#ifdef PROTON_HAVE_NGSPICE
        sim::NgspiceEngine::Config config;
        // A chunk past the duration, so the chunk that reaches the duration is never cut short
        // by the end of the run.
        const double chunk = sim::SimWorker::sanitise(settings.worker).chunkDuration;
        config.stopTime = settings.duration + chunk;
        config.maxStep = settings.maxStep;
        return std::make_unique<sim::NgspiceEngine>(config);
#else
        error = "this build has no ngspice";
        return nullptr;
#endif
    }

    // The mock engine simulates the same circuit, set up from the same settings.
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
    Circuit circuit;
    circuit.components = {
        Component{ComponentKind::Resistor, "R1", {kInputNet, kOutputNet}, example.resistance},
        Component{ComponentKind::Capacitor,
                  "C1",
                  {kOutputNet, std::string(schematic::kGroundNet)},
                  example.capacitance},
    };
    const FunctionGenerator generator(example.generator);
    circuit.fragments = {
        {kGenerator, generator.toSpice(kGenerator, kInputNet, schematic::kGroundNet).lines},
    };
    return circuit;
}

PipelineResult run(const RcExample& example, const PipelineSettings& settings)
{
    PipelineResult result;
    const Circuit circuit = buildCircuit(example);
    result.diagnostics = schematic::validate(circuit);
    if (schematic::hasErrors(result.diagnostics)) {
        const auto error = std::find_if(result.diagnostics.begin(), result.diagnostics.end(),
                                        [](const schematic::Diagnostic& d) {
                                            return d.severity == schematic::Severity::Error;
                                        });
        result.error = error->message;
        return result;
    }
    result.netlist = schematic::buildNetlist(circuit);

    std::unique_ptr<sim::ISimEngine> engine = makeEngine(example, settings, result.error);
    if (!engine) {
        return result;
    }
    sim::SimWorker worker(std::move(engine), settings.worker);
    if (!worker.start(result.netlist)) {
        result.error = worker.error();
        return result;
    }
    const std::optional<std::size_t> in =
        sim::indexOfSignal(worker.signalNames(), std::string("v(") + kInputNet + ")");
    const std::optional<std::size_t> out =
        sim::indexOfSignal(worker.signalNames(), std::string("v(") + kOutputNet + ")");
    if (!in || !out) {
        result.error = "the simulation has no v(in) or v(out)";
        return result;
    }

    Pump pump(settings, result);
    bool done = false;
    while (!done) {
        if (std::optional<sim::SimChunk> chunk = worker.tryPop()) {
            done = pump.feed(*chunk, *in, *out);
            continue;
        }
        if (!worker.running()) {
            // The run failed: every chunk it produced is already in the ring.
            while (std::optional<sim::SimChunk> chunk = worker.tryPop()) {
                pump.feed(*chunk, *in, *out);
            }
            result.error = worker.error();
            break;
        }
        std::this_thread::yield();
    }
    worker.stop();
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
         << ", " << number(settings.duration) << " s\n";
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
