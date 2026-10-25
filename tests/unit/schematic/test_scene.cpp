// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/netlist.h"
#include "proton/schematic/scene.h"
#include "proton/schematic/validate.h"
#include "proton/test/test.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace proton::schematic;

namespace {

PlacedPart part(ComponentKind kind, std::string designator, std::string value, Point at,
                Rotation rotation = Rotation::R0)
{
    return {kind, std::move(designator), std::move(value), {at, rotation, false}};
}

PlacedPart ground(Point at)
{
    return {ComponentKind::Ground, "", "", {at, Rotation::R0, false}};
}

// The nets of the component called `designator`.
std::vector<std::string> netsOf(const Conversion& conversion, const std::string& designator)
{
    for (const Component& component : conversion.circuit.components) {
        if (component.designator == designator) {
            return component.nets;
        }
    }
    return {};
}

bool hasCode(const std::vector<Diagnostic>& diagnostics, DiagnosticCode code)
{
    return std::ranges::any_of(diagnostics,
                               [code](const Diagnostic& d) { return d.code == code; });
}

// The voltage divider: V1 upright at (0, 0) with + at (0, -2) and - at (0, 2); R1 horizontal from
// (2, -2) to (6, -2); R2 upright (turned) from (8, 0) to (8, 4)... drawn with wires:
//
//      (0,-2)----(2,-2) R1 (6,-2)----(8,-2)
//        V1                            |
//      (0, 2)                        (8,-2) R2 (turned) (8, 2)
//        |                             |
//      ground (0,4) ------------------(8,4) wire, ground
Scene divider()
{
    Scene scene;
    scene.parts = {
        part(ComponentKind::VoltageSource, "V1", "5", {0, 0}),
        part(ComponentKind::Resistor, "R1", "10k", {4, -2}),
        part(ComponentKind::Resistor, "R2", "10k", {8, 0}, Rotation::R90), // pins (8,-2), (8,2)
        ground({0, 4}),
    };
    scene.wires = {
        {{0, -2}, {2, -2}}, // V1 + to R1
        {{6, -2}, {8, -2}}, // R1 to R2
        {{0, 2}, {0, 4}},   // V1 - to ground
        {{8, 2}, {8, 4}},   // R2 down
        {{8, 4}, {0, 4}},   // along the bottom to ground
    };
    scene.labels = {{"in", {1, -2}}, {"out", {7, -2}}};
    return scene;
}

} // namespace

TEST(pins_turn_and_mirror_with_their_part)
{
    const Placement upright{{10, 10}, Rotation::R90, false};
    CHECK(place({-2, 0}, upright) == (Point{10, 8}));
    CHECK(place({2, 0}, upright) == (Point{10, 12}));
    const Placement mirrored{{0, 0}, Rotation::R0, true};
    CHECK(place({2, -1}, mirrored) == (Point{-2, -1}));
    const Placement half{{0, 0}, Rotation::R180, false};
    CHECK(place({0, -2}, half) == (Point{0, 2}));
    CHECK(place({1, 2}, {{0, 0}, Rotation::R270, false}) == (Point{2, -1}));
}

TEST(the_divider_converts_to_its_circuit)
{
    const Conversion conversion = convert(divider());
    CHECK(conversion.diagnostics.empty());
    CHECK(netsOf(conversion, "V1") == (std::vector<std::string>{"in", "0"}));
    CHECK(netsOf(conversion, "R1") == (std::vector<std::string>{"in", "out"}));
    CHECK(netsOf(conversion, "R2") == (std::vector<std::string>{"out", "0"}));
    CHECK(validate(conversion.circuit).empty());
    CHECK_EQ(buildNetlist(conversion.circuit),
             std::string("* Proton netlist\nR1 in out 10k\nR2 out 0 10k\nV1 in 0 DC 5\n"));
}

TEST(crossing_wires_connect_only_at_a_junction)
{
    Scene scene;
    scene.parts = {
        part(ComponentKind::Resistor, "R1", "1k", {-4, 0}),  // pins (-6,0), (-2,0)
        part(ComponentKind::Resistor, "R2", "1k", {4, 0}),   // pins (2,0), (6,0)
        part(ComponentKind::Resistor, "R3", "1k", {0, -4}, Rotation::R90), // (0,-6), (0,-2)
        part(ComponentKind::Resistor, "R4", "1k", {0, 4}, Rotation::R90),  // (0,2), (0,6)
    };
    scene.wires = {{{-2, 0}, {2, 0}}, {{0, -2}, {0, 2}}}; // a plus sign, crossing at (0, 0)
    const Conversion crossing = convert(scene);
    CHECK(netsOf(crossing, "R1")[1] == netsOf(crossing, "R2")[0]);
    CHECK(netsOf(crossing, "R3")[1] == netsOf(crossing, "R4")[0]);
    CHECK(netsOf(crossing, "R1")[1] != netsOf(crossing, "R3")[1]);

    scene.junctions = {{0, 0}};
    const Conversion joined = convert(scene);
    CHECK(netsOf(joined, "R1")[1] == netsOf(joined, "R3")[1]);
}

TEST(a_wire_ending_on_another_wire_connects)
{
    Scene scene;
    scene.parts = {
        part(ComponentKind::Resistor, "R1", "1k", {-4, 0}), // pins (-6,0), (-2,0)
        part(ComponentKind::Resistor, "R2", "1k", {4, 0}),  // pins (2,0), (6,0)
        part(ComponentKind::Resistor, "R3", "1k", {0, 4}, Rotation::R90), // (0,2), (0,6)
    };
    scene.wires = {{{-2, 0}, {2, 0}}, {{0, 0}, {0, 2}}}; // a T: the second ends on the first
    const Conversion conversion = convert(scene);
    CHECK(netsOf(conversion, "R1")[1] == netsOf(conversion, "R3")[0]);
    CHECK(netsOf(conversion, "R2")[0] == netsOf(conversion, "R3")[0]);
}

TEST(labels_with_the_same_name_are_one_net)
{
    Scene scene;
    scene.parts = {
        part(ComponentKind::Resistor, "R1", "1k", {0, 0}),  // pins (-2,0), (2,0)
        part(ComponentKind::Resistor, "R2", "1k", {20, 0}), // pins (18,0), (22,0)
    };
    scene.labels = {{"mid", {2, 0}}, {"MID", {18, 0}}};
    const Conversion conversion = convert(scene);
    CHECK(conversion.diagnostics.empty());
    CHECK_EQ(netsOf(conversion, "R1")[1], std::string("mid")); // first spelling wins
    CHECK_EQ(netsOf(conversion, "R2")[0], std::string("mid"));
}

TEST(two_labels_on_one_net_are_an_error)
{
    Scene scene = divider();
    scene.labels.push_back({"top", {0, -2}}); // on `in`, which is already labelled
    const Conversion conversion = convert(scene);
    CHECK(hasCode(conversion.diagnostics, DiagnosticCode::ConflictingLabels));
    CHECK_EQ(netsOf(conversion, "R1")[0], std::string("in"));
}

TEST(a_label_on_ground_is_an_error)
{
    Scene scene = divider();
    scene.labels.push_back({"gnd", {0, 4}});
    CHECK(hasCode(convert(scene).diagnostics, DiagnosticCode::ConflictingLabels));
}

TEST(unnamed_nets_are_numbered_by_position_whatever_the_drawing_order)
{
    Scene scene = divider();
    scene.labels.clear();
    const Conversion first = convert(scene);
    CHECK(netsOf(first, "R1") == (std::vector<std::string>{"N1", "N2"}));
    std::ranges::reverse(scene.parts);
    std::ranges::reverse(scene.wires);
    for (Wire& wire : scene.wires) {
        std::swap(wire.from, wire.to);
    }
    const Conversion second = convert(scene);
    CHECK(netsOf(second, "R1") == netsOf(first, "R1"));
    CHECK(netsOf(second, "R2") == netsOf(first, "R2"));
    CHECK(netsOf(second, "V1") == netsOf(first, "V1"));
}

TEST(instruments_get_the_nets_of_their_terminals)
{
    Scene scene = divider();
    // scope1 at (-6, -1): CH1 at (-9, -2), CH2 at (-9, 0). CH1 wired to `in`, CH2 left open.
    scene.instruments = {{InstrumentKind::Scope, "scope1", {{-6, -1}, Rotation::R0, false}},
                         // dmm1 with + at (7, 6) and COM at (9, 6), wired to `out` and ground.
                         {InstrumentKind::Multimeter, "dmm1", {{8, 4}, Rotation::R0, false}}};
    scene.wires.push_back({{-9, -2}, {0, -2}});
    scene.wires.push_back({{7, 6}, {7, -2}}); // up to the R1-R2 wire, ending on it
    scene.wires.push_back({{9, 6}, {9, 4}});
    scene.wires.push_back({{9, 4}, {8, 4}});
    const Conversion conversion = convert(scene);
    CHECK_EQ(conversion.instruments.size(), 2u);
    if (conversion.instruments.size() == 2) {
        CHECK(conversion.instruments[0].nets == (std::vector<std::string>{"in", ""}));
        CHECK(conversion.instruments[1].nets == (std::vector<std::string>{"out", "0"}));
    }
    // Instruments are not components of the circuit.
    CHECK_EQ(conversion.circuit.components.size(), 4u);
    CHECK_EQ(conversion.diagnostics.size(), 1u);
    if (conversion.diagnostics.size() == 1) {
        const Diagnostic& unconnected = conversion.diagnostics[0];
        CHECK(unconnected.code == DiagnosticCode::UnconnectedTerminal);
        CHECK(unconnected.severity == Severity::Warning);
        CHECK_EQ(unconnected.message, std::string("CH2 of scope scope1 is not connected to the circuit."));
    }
}

TEST(a_terminal_wired_only_to_another_instrument_is_unconnected)
{
    Scene scene;
    scene.instruments = {{InstrumentKind::Generator, "gen1", {{0, 0}, Rotation::R0, false}},
                         {InstrumentKind::Scope, "scope1", {{5, 0}, Rotation::R0, false}}};
    // gen1's + at (2,-1) straight to scope1's CH1 at (2,-1): no circuit in between.
    const Conversion conversion = convert(scene);
    CHECK(conversion.instruments[0].nets[0].empty());
    CHECK(conversion.instruments[1].nets[0].empty());
    CHECK_EQ(std::ranges::count_if(conversion.diagnostics,
                                   [](const Diagnostic& d) {
                                       return d.code == DiagnosticCode::UnconnectedTerminal;
                                   }),
             4);
}

TEST(the_simulator_s_own_names_are_reserved)
{
    Scene scene = divider();
    scene.labels[1].name = "PROTON_HALT";
    const std::vector<Diagnostic> diagnostics = validate(convert(scene).circuit);
    CHECK(hasCode(diagnostics, DiagnosticCode::ReservedName));

    Circuit circuit = convert(divider()).circuit;
    circuit.components[0].designator = "Vproton_halt";
    CHECK(hasCode(validate(circuit), DiagnosticCode::ReservedName));
}
