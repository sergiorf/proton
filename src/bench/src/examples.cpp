// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/examples.h"

#include <string>
#include <utility>

namespace proton::bench {

namespace {

using instruments::FunctionGenerator;
using instruments::Multimeter;
using instruments::Scope;
using schematic::ComponentKind;
using schematic::PlacedPart;
using schematic::Point;
using schematic::Rotation;
using schematic::Scene;

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

PlacedPart part(ComponentKind kind, std::string designator, std::string value, Point at,
                Rotation rotation = Rotation::R0)
{
    return {kind, std::move(designator), std::move(value), {at, rotation, false}};
}

Example make(std::string name, Scene scene, Bench::Config bench)
{
    Example example{std::move(name), std::move(scene), {}, std::move(bench)};
    example.circuit = schematic::convert(example.scene).circuit;
    return example;
}

// A filter: `series` from `in` to `out`, `shunt` from `out` to ground, as drawn:
//
//     in o----[series]----o out
//                         |
//                      [shunt]
//                         |
//                        GND
Scene filter(PlacedPart series, PlacedPart shunt)
{
    series.placement = {{4, 0}, Rotation::R0, false}; // pins (2, 0), (6, 0)
    shunt.placement = {{8, 2}, Rotation::R90, false}; // pins (8, 0), (8, 4)
    Scene scene;
    scene.parts = {std::move(series), std::move(shunt),
                   part(ComponentKind::Ground, "", "", {8, 6})};
    scene.wires = {{{0, 0}, {2, 0}}, {{6, 0}, {8, 0}}, {{8, 4}, {8, 6}}};
    scene.labels = {{"in", {0, 0}}, {"out", {8, 0}}};
    return scene;
}

// The divider: V1 on the left, R1 across the top, R2 down the right, ground along the bottom.
Scene divider()
{
    Scene scene;
    scene.parts = {
        part(ComponentKind::VoltageSource, "V1", "5", {0, 2}),              // pins (0,0), (0,4)
        part(ComponentKind::Resistor, "R1", "10k", {4, 0}),                 // pins (2,0), (6,0)
        part(ComponentKind::Resistor, "R2", "10k", {8, 2}, Rotation::R90), // pins (8,0), (8,4)
        part(ComponentKind::Ground, "", "", {4, 6}),
    };
    scene.wires = {{{0, 0}, {2, 0}}, {{6, 0}, {8, 0}}, {{0, 4}, {0, 6}}, {{0, 6}, {8, 6}},
                   {{8, 4}, {8, 6}}};
    scene.labels = {{"in", {1, 0}}, {"out", {7, 0}}};
    return scene;
}

} // namespace

std::vector<Example> examples()
{
    Example lowPass = make("RC low-pass",
                           filter(part(ComponentKind::Resistor, "R1", "1k", {}),
                                  part(ComponentKind::Capacitor, "C1", "100n", {})),
                           wiring(Multimeter::Mode::AcVoltage));

    Bench::Config dividerBench = wiring(Multimeter::Mode::DcVoltage);
    dividerBench.generators[0].plus.clear(); // on the bench, not connected
    Example dividerExample = make("Voltage divider", divider(), dividerBench);

    Example highPass = make("RC high-pass",
                            filter(part(ComponentKind::Capacitor, "C1", "100n", {}),
                                   part(ComponentKind::Resistor, "R1", "1k", {})),
                            wiring(Multimeter::Mode::AcVoltage));

    return {lowPass, dividerExample, highPass};
}

} // namespace proton::bench
