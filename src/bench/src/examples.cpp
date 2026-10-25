// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/examples.h"

namespace proton::bench {

namespace {

using instruments::FunctionGenerator;
using instruments::Multimeter;
using instruments::Scope;
using schematic::Component;
using schematic::ComponentKind;

const std::string kGround(schematic::kGroundNet);

// CH1 on `in`, CH2 on `out`, 200 us/div (two periods of 1 kHz), 1 V/div, Auto, triggered by CH1
// rising through 0 V; the multimeter on `out` in `mode`.
Bench::Config wiring(Multimeter::Mode mode)
{
    Bench::Config bench;
    bench.generator = FunctionGenerator::Config{
        .waveform = FunctionGenerator::Waveform::Sine,
        .frequency = 1e3,
        .amplitude = 1.0,
        .offset = 0.0,
        .outputImpedance = 50.0,
    };
    bench.generatorNet = "in";
    bench.scope.timePerDiv = 200e-6;
    bench.scope.mode = Scope::Mode::Auto;
    bench.scope.trigger.source = Scope::TriggerSource::Channel1;
    bench.scope.trigger.slope = Scope::Slope::Rising;
    bench.scope.trigger.level = 0.0;
    bench.scopeNets = {"in", "out"};
    bench.multimeters = {{Multimeter::Config{}, "out"}};
    bench.multimeters[0].config.mode = mode;
    return bench;
}

} // namespace

std::vector<Example> examples()
{
    Example lowPass{"RC low-pass", {}, wiring(Multimeter::Mode::AcVoltage)};
    lowPass.circuit.components = {
        Component{ComponentKind::Resistor, "R1", {"in", "out"}, "1k"},
        Component{ComponentKind::Capacitor, "C1", {"out", kGround}, "100n"},
    };

    Example divider{"Voltage divider", {}, wiring(Multimeter::Mode::DcVoltage)};
    divider.bench.generatorNet.clear();
    divider.circuit.components = {
        Component{ComponentKind::VoltageSource, "V1", {"in", kGround}, "5"},
        Component{ComponentKind::Resistor, "R1", {"in", "out"}, "10k"},
        Component{ComponentKind::Resistor, "R2", {"out", kGround}, "10k"},
    };

    Example highPass{"RC high-pass", {}, wiring(Multimeter::Mode::AcVoltage)};
    highPass.circuit.components = {
        Component{ComponentKind::Capacitor, "C1", {"in", "out"}, "100n"},
        Component{ComponentKind::Resistor, "R1", {"out", kGround}, "1k"},
    };

    return {lowPass, divider, highPass};
}

} // namespace proton::bench
