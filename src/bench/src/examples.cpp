// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/examples.h"

#include "proton/bench/sheet_bench.h"
#include "proton/schematic/scene_edit.h"

#include <string>
#include <utility>

namespace proton::bench {

namespace {

using instruments::FunctionGenerator;
using instruments::Multimeter;
using instruments::Scope;
using schematic::ComponentKind;
using schematic::InstrumentKind;
using schematic::PlacedInstrument;
using schematic::PlacedPart;
using schematic::Point;
using schematic::Rotation;
using schematic::Scene;

const std::string kGround(schematic::kGroundNet);

// The instruments' settings, by name; their nets come from the drawing: gen1 1 kHz sine, 1 V,
// ideal (0 ohm); scope1 200 us/div (two periods of 1 kHz), 1 V/div, Auto, triggered by CH1 rising through
// 0 V; dmm1 in `mode`.
Bench::Config settings(Multimeter::Mode mode)
{
    Bench::GeneratorSettings generator;
    generator.config = FunctionGenerator::Config{
        .waveform = FunctionGenerator::Waveform::Sine,
        .frequency = 1e3,
        .amplitude = 1.0,
        .offset = 0.0,
        .outputImpedance = 0.0,
        .output = true,
    };

    Bench::ScopeSettings scope;
    scope.config.timePerDiv = 200e-6;
    scope.config.mode = Scope::Mode::Auto;
    scope.config.trigger.source = Scope::TriggerSource::Channel1;
    scope.config.trigger.slope = Scope::Slope::Rising;
    scope.config.trigger.level = 0.0;

    Bench::MultimeterSettings multimeter;
    multimeter.config.mode = mode;

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

PlacedInstrument instrument(InstrumentKind kind, std::string name, Point at)
{
    return {kind, std::move(name), {at, Rotation::R0, false}};
}

void addWire(Scene& scene, Point from, Point to)
{
    for (const schematic::Wire& wire : schematic::route(from, to)) {
        scene.wires.push_back(wire);
    }
}

// The instruments beside a circuit whose `out` node is at (8, 0): scope1 on the right, CH2 wired
// to (8, 0) and CH1 to an `in` label; dmm1 below it, clear of the shunt part's name, + to an `out`
// label and COM to a ground of its own; and with `generator`, gen1 on the left driving (0, 0), its
// COM on a ground of its own.
void addInstruments(Scene& scene, bool generator)
{
    scene.instruments.push_back(instrument(InstrumentKind::Scope, "scope1", {17, -1}));
    addWire(scene, {8, 0}, {14, 0});   // CH2
    addWire(scene, {14, -2}, {12, -2}); // CH1
    scene.labels.push_back({"in", {12, -2}});
    scene.instruments.push_back(instrument(InstrumentKind::Multimeter, "dmm1", {16, 7}));
    addWire(scene, {15, 9}, {15, 11}); // +
    addWire(scene, {15, 11}, {12, 11});
    scene.labels.push_back({"out", {12, 11}});
    addWire(scene, {17, 9}, {17, 11}); // COM
    scene.parts.push_back(part(ComponentKind::Ground, "", "", {17, 11}));
    if (generator) {
        scene.instruments.push_back(instrument(InstrumentKind::Generator, "gen1", {-6, 1}));
        addWire(scene, {-4, 0}, {0, 0}); // +
        addWire(scene, {-4, 2}, {-4, 4}); // COM
        scene.parts.push_back(part(ComponentKind::Ground, "", "", {-4, 4}));
    }
}

Example make(std::string name, Scene scene, const Bench::Config& settings)
{
    Example example{std::move(name), std::move(scene), {}, {}};
    const schematic::Conversion conversion = schematic::convert(example.scene);
    example.circuit = conversion.circuit;
    example.bench = benchFor(conversion.instruments, settings);
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
    addInstruments(scene, true);
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
    addInstruments(scene, false);
    return scene;
}

} // namespace

std::vector<Example> examples()
{
    Example lowPass = make("RC low-pass",
                           filter(part(ComponentKind::Resistor, "R1", "1k", {}),
                                  part(ComponentKind::Capacitor, "C1", "100n", {})),
                           settings(Multimeter::Mode::AcVoltage));

    Example dividerExample =
        make("Voltage divider", divider(), settings(Multimeter::Mode::DcVoltage));

    Example highPass = make("RC high-pass",
                            filter(part(ComponentKind::Capacitor, "C1", "100n", {}),
                                   part(ComponentKind::Resistor, "R1", "1k", {})),
                            settings(Multimeter::Mode::AcVoltage));

    return {lowPass, dividerExample, highPass};
}

} // namespace proton::bench
