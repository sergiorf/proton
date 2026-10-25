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

// gen1 between `in` and ground; scope1 with CH1 on `in`, CH2 on `out`, 200 us/div (two periods of
// 1 kHz), 1 V/div, Auto, triggered by CH1 rising through 0 V; dmm1 between `out` and ground in
// `mode`.
Bench::Config wiring(Multimeter::Mode mode)
{
    Bench::GeneratorSettings generator;
    generator.config = FunctionGenerator::Config{
        .waveform = FunctionGenerator::Waveform::Sine,
        .frequency = 1e3,
        .amplitude = 1.0,
        .offset = 0.0,
        .outputImpedance = 50.0,
        .output = true,
    };
    generator.plus = "in";
    generator.com = kGround;

    Bench::ScopeSettings scope;
    scope.config.timePerDiv = 200e-6;
    scope.config.mode = Scope::Mode::Auto;
    scope.config.trigger.source = Scope::TriggerSource::Channel1;
    scope.config.trigger.slope = Scope::Slope::Rising;
    scope.config.trigger.level = 0.0;
    scope.channels = {"in", "out"};

    Bench::MultimeterSettings multimeter;
    multimeter.config.mode = mode;
    multimeter.plus = "out";
    multimeter.com = kGround;

    Bench::Config bench;
    bench.generators = {generator};
    bench.scopes = {scope};
    bench.multimeters = {multimeter};
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
    divider.bench.generators[0].plus.clear(); // on the bench, not connected
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
