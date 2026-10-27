// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/diagnostic_place.h"
#include "proton/schematic/scene.h"
#include "proton/schematic/validate.h"
#include "proton/test/test.h"

#include <cstddef>
#include <optional>
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

// The voltage divider of test_scene.cpp: V1 upright at (0, 0), + at (0, -2); R1 from (2, -2) to
// (6, -2); R2 turned from (8, -2) to (8, 2); ground at (0, 4); labels `in` at (1, -2) and `out`
// at (7, -2). Parts 0 V1, 1 R1, 2 R2, 3 ground; wire 0 from V1 + to R1.
Scene divider()
{
    Scene scene;
    scene.parts = {
        part(ComponentKind::VoltageSource, "V1", "5", {0, 0}),
        part(ComponentKind::Resistor, "R1", "10k", {4, -2}),
        part(ComponentKind::Resistor, "R2", "10k", {8, 0}, Rotation::R90),
        ground({0, 4}),
    };
    scene.wires = {
        {{0, -2}, {2, -2}},
        {{6, -2}, {8, -2}},
        {{0, 2}, {0, 4}},
        {{8, 2}, {8, 4}},
        {{8, 4}, {0, 4}},
    };
    scene.labels = {{"in", {1, -2}}, {"out", {7, -2}}};
    return scene;
}

// Every diagnostic of the drawing, as the editor gets them: the conversion's, then validate()'s.
std::vector<Diagnostic> diagnose(const Conversion& conversion)
{
    std::vector<Diagnostic> all = conversion.diagnostics;
    for (Diagnostic& diagnostic : validate(conversion.circuit)) {
        all.push_back(std::move(diagnostic));
    }
    return all;
}

// The place of the first diagnostic with `code`; none if the drawing has no such diagnostic.
std::optional<DiagnosticPlace> placeOf(const Scene& scene, DiagnosticCode code)
{
    const Conversion conversion = convert(scene);
    for (const Diagnostic& diagnostic : diagnose(conversion)) {
        if (diagnostic.code == code) {
            return locate(scene, conversion, diagnostic);
        }
    }
    return std::nullopt;
}

using Indices = std::vector<std::size_t>;
using Points = std::vector<Point>;

} // namespace

TEST(a_clean_drawing_has_no_diagnostics_to_place)
{
    CHECK(diagnose(convert(divider())).empty());
}

TEST(no_ground_is_on_the_whole_drawing)
{
    Scene scene = divider();
    scene.parts.pop_back();
    const std::optional<DiagnosticPlace> place = placeOf(scene, DiagnosticCode::NoGround);
    CHECK(place && place->empty());
}

TEST(an_invalid_designator_is_on_its_part)
{
    Scene scene = divider();
    scene.parts[1].designator = "X1";
    std::optional<DiagnosticPlace> place = placeOf(scene, DiagnosticCode::InvalidDesignator);
    CHECK(place && place->parts == Indices{1} && place->pins.empty() && place->wires.empty());
    // A part with no designator, not the ground symbol, which has none either.
    scene.parts[1].designator.clear();
    place = placeOf(scene, DiagnosticCode::InvalidDesignator);
    CHECK(place && place->parts == Indices{1});
}

TEST(a_duplicate_designator_is_on_both_parts_ignoring_case)
{
    Scene scene = divider();
    scene.parts[2].designator = "r1";
    const std::optional<DiagnosticPlace> place =
        placeOf(scene, DiagnosticCode::DuplicateDesignator);
    CHECK(place && place->parts == (Indices{1, 2}));
}

TEST(an_invalid_or_reserved_net_name_is_on_its_net)
{
    for (const auto& [name, code] :
         {std::pair{std::string("a/b"), DiagnosticCode::InvalidNetName},
          std::pair{std::string("proton_halt"), DiagnosticCode::ReservedName}}) {
        Scene scene = divider();
        scene.labels[0].name = name;
        const std::optional<DiagnosticPlace> place = placeOf(scene, code);
        CHECK(place.has_value());
        if (place) {
            CHECK(place->labels == Indices{0});
            CHECK(place->wires == Indices{0});
            CHECK(place->pins == (Points{{0, -2}, {2, -2}})); // V1 + and R1's left pin
            CHECK(place->parts.empty());
        }
    }
}

TEST(an_invalid_or_zero_value_is_on_its_part)
{
    Scene scene = divider();
    scene.parts[1].value = "abc";
    scene.parts[2].value = "0";
    std::optional<DiagnosticPlace> place = placeOf(scene, DiagnosticCode::InvalidValue);
    CHECK(place && place->parts == Indices{1});
    place = placeOf(scene, DiagnosticCode::ZeroValue);
    CHECK(place && place->parts == Indices{2});
}

TEST(a_single_connection_is_on_its_lone_pin)
{
    Scene scene = divider();
    scene.parts.push_back(part(ComponentKind::Resistor, "R3", "1k", {8, 10})); // (6,10), (10,10)
    scene.wires.push_back({{6, 10}, {6, 4}}); // its left pin to ground
    const std::optional<DiagnosticPlace> place =
        placeOf(scene, DiagnosticCode::SingleConnectionNet);
    CHECK(place && place->pins == Points{{10, 10}} && place->wires.empty() &&
          place->parts.empty());
}

TEST(no_dc_path_is_on_the_floating_net)
{
    // out -- C1 -- (14,-2) wire (16,-2) -- C2 -- ground: the node between them floats.
    Scene scene = divider();
    scene.parts.push_back(part(ComponentKind::Capacitor, "C1", "1u", {12, -2})); // (10,-2), (14,-2)
    scene.parts.push_back(part(ComponentKind::Capacitor, "C2", "1u", {16, 0}, Rotation::R90));
    scene.parts.push_back(ground({16, 2}));
    scene.wires.push_back({{8, -2}, {10, -2}});
    scene.wires.push_back({{14, -2}, {16, -2}}); // wire 6
    const std::optional<DiagnosticPlace> place = placeOf(scene, DiagnosticCode::NoDcPathToGround);
    CHECK(place && place->wires == Indices{6} && place->pins == (Points{{14, -2}, {16, -2}}));
}

TEST(an_unconnected_terminal_is_on_its_instrument_and_that_terminal)
{
    Scene scene = divider();
    // scope1 with CH1 at (27, -1) on `out` by a label, CH2 at (27, 1) wired to nothing.
    scene.instruments.push_back({InstrumentKind::Scope, "scope1", {{30, 0}, Rotation::R0, false}});
    scene.labels.push_back({"out", {27, -1}});
    const std::optional<DiagnosticPlace> place =
        placeOf(scene, DiagnosticCode::UnconnectedTerminal);
    CHECK(place && place->instruments == Indices{0} && place->pins == Points{{27, 1}});
}

TEST(conflicting_labels_are_on_the_label_that_conflicts)
{
    Scene scene = divider();
    scene.labels.push_back({"vout", {8, -2}}); // on `out`'s wire end
    const std::optional<DiagnosticPlace> place =
        placeOf(scene, DiagnosticCode::ConflictingLabels);
    CHECK(place && place->labels == Indices{2});
}

TEST(an_instrument_fragment_element_is_on_its_instrument)
{
    Scene scene = divider();
    scene.instruments.push_back(
        {InstrumentKind::Generator, "gen1", {{-10, 0}, Rotation::R0, false}});
    const Conversion conversion = convert(scene);
    const Diagnostic named{Severity::Error, DiagnosticCode::DuplicateDesignator, "VGEN1", {}, ""};
    CHECK(locate(scene, conversion, named).instruments == Indices{0});
    const Diagnostic byName{Severity::Error, DiagnosticCode::DuplicateDesignator, "gen1", {}, ""};
    CHECK(locate(scene, conversion, byName).instruments == Indices{0});
}

TEST(a_place_contains_its_symbols_wires_labels_and_pins)
{
    Scene scene = divider();
    scene.parts[1].value = "abc";
    scene.labels[0].name = "a/b";
    const std::optional<DiagnosticPlace> part = placeOf(scene, DiagnosticCode::InvalidValue);
    const std::optional<DiagnosticPlace> net = placeOf(scene, DiagnosticCode::InvalidNetName);
    CHECK(part && net);
    if (part && net) {
        CHECK(contains(scene, *part, {4, -2}));  // R1's symbol
        CHECK(!contains(scene, *part, {8, 0})); // R2's
        CHECK(contains(scene, *net, {1, -2}));   // the label, on the wire
        CHECK(contains(scene, *net, {0, -2}));   // V1's + pin
        CHECK(!contains(scene, *net, {7, -2}));  // `out`
    }
}
