// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/instruments/function_generator.h"
#include "proton/instruments/multimeter.h"
#include "proton/instruments/scope.h"
#include "proton/schematic/circuit.h"
#include "proton/schematic/validate.h"
#include "proton/sim/sim_worker.h"

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace proton::spike {

// The whole pipeline, headless (ARCHITECTURE.md section 3): the RC example on the bench
// (bench::RunControl), with MockEngine or NgspiceEngine on the worker thread and the instruments on
// the calling thread. Shared by proton-spike and the integration test, so the test checks exactly
// what the demo runs.

// The RC example: a function generator on net `in`, R1 from `in` to `out`, C1 from `out` to
// ground. The defaults give an RC low-pass with tau = 100 us (corner 1.59 kHz) driven by a
// 1 kHz sine of 1 V around 0.5 V.
//
// The mock engine does not read the netlist: it is set up from these same settings. So that the
// netlist and the mock describe the same circuit, the generator must be a sine and its output
// impedance is not modelled by the mock; it is 0 by default, which writes no resistor. ngspice
// simulates the netlist, so it takes any generator settings.
struct RcExample
{
    instruments::FunctionGenerator::Config generator{
        .waveform = instruments::FunctionGenerator::Waveform::Sine,
        .frequency = 1e3,
        .amplitude = 1.0,
        .offset = 0.5,
        .outputImpedance = 0.0,
    };
    std::string resistance = "1k";    // R1, as a student would type it
    std::string capacitance = "100n"; // C1

    friend bool operator==(const RcExample&, const RcExample&) = default;
};

struct PipelineSettings
{
    // The simulator. ngspice is available only in builds that have it (PROTON_HAVE_NGSPICE);
    // elsewhere choosing it makes run() fail with an error.
    enum class Engine {
        Mock,    // MockEngine, set up from the example's settings
        Ngspice, // NgspiceEngine, reading the netlist
    };

    Engine engine = Engine::Mock;
    double duration = 0.35; // s of simulation time; points after it are not fed
    // Feed the instruments in step with wall-clock time (RealTimePacer) instead of as fast as the
    // engine runs. Changes when the instruments are fed, never what they give.
    bool realTime = false;
    sim::SimWorker::Config worker;
    // CH1 on v(in), CH2 on v(out). Both channels at 1 V/div, 200 us/div (two periods of 1 kHz on
    // the screen), Normal mode, triggered by CH1 rising through the generator's offset; see
    // defaultSettings(). The engine's longest step is bench::maxStepFor() it.
    instruments::Scope::Config scope;
    double gateTime = 0.1; // s, for both multimeters: 100 periods of 1 kHz
    // For tests of error handling: the mock engine fails after this simulation time. Ignored by
    // ngspice.
    double failAt = std::numeric_limits<double>::infinity();
};

// The engine named on proton-spike's command line: "mock" or "ngspice", exactly. Empty for any
// other name.
std::optional<PipelineSettings::Engine> parseEngine(std::string_view name);

// The settings proton-spike uses for `example`.
PipelineSettings defaultSettings(const RcExample& example);

// The example's circuit, with the generator's fragment as the bench contributes it ("gen1").
schematic::Circuit buildCircuit(const RcExample& example);

struct PipelineResult
{
    std::vector<schematic::Diagnostic> diagnostics; // from validate()
    std::string netlist;                            // empty when validate() found errors
    // Everything the instruments gave, in order. The run pops after every chunk's worth of
    // simulation time, so with chunks shorter than a frame and a gate, none is missed.
    std::vector<instruments::ScopeFrame> frames;
    std::vector<instruments::DmmReading> dcReadings; // DMM1, DC V on v(out)
    std::vector<instruments::DmmReading> acReadings; // DMM2, AC V on v(out)
    double lastTime = 0.0;                           // simulation time of the last point fed
    // With realTime, the pacer's speed at the end of the run (RealTimePacer::speed()); 1 without.
    double speed = 1.0;
    std::string error; // why the run stopped early, empty when it reached the duration

    bool ok() const { return error.empty(); }
};

// Runs the example to `settings.duration`. Deterministic: the same example and settings give the
// same result, whatever the chunk duration and queue capacity, as long as chunks are shorter
// than a scope frame and a multimeter gate.
PipelineResult run(const RcExample& example, const PipelineSettings& settings);

// One frame as CSV: a header, then one row per sample with its time relative to the trigger and
// both channels in volts. Numbers are the shortest text that reads back as the same double.
//
//     t_rel_s,ch1_v_in,ch2_v_out
//     -0.001,0.5,0.87...
std::string frameCsv(const instruments::ScopeFrame& frame);

// What proton-spike prints: a commented header (example, netlist, multimeter readings, frame
// summary) followed by frameCsv() of the last frame, or a commented error.
std::string report(const RcExample& example, const PipelineSettings& settings,
                   const PipelineResult& result);

} // namespace proton::spike
