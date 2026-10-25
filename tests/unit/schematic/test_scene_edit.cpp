// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/netlist.h"
#include "proton/schematic/scene.h"
#include "proton/schematic/scene_edit.h"
#include "proton/schematic/validate.h"
#include "proton/test/test.h"

#include <string>
#include <vector>

using namespace proton::schematic;

namespace {

PlacedPart part(ComponentKind kind, std::string designator, Point at,
                Rotation rotation = Rotation::R0)
{
    return {kind, std::move(designator), defaultValue(kind), {at, rotation, false}};
}

void addRoute(Scene& scene, Point from, Point to)
{
    for (const Wire& wire : route(from, to)) {
        scene.wires.push_back(wire);
    }
}

} // namespace

TEST(bounds_cover_the_symbol_turned_with_it)
{
    const PlacedPart r = part(ComponentKind::Resistor, "R1", {10, 10});
    CHECK(bounds(r) == (Box{8, 9, 12, 11}));
    const PlacedPart turned = part(ComponentKind::Resistor, "R1", {10, 10}, Rotation::R90);
    CHECK(bounds(turned) == (Box{9, 8, 11, 12}));
    CHECK(bounds(part(ComponentKind::Ground, "", {0, 0})) == (Box{-1, 0, 1, 2}));
}

TEST(hit_testing_finds_the_topmost_part_and_wires_along_their_length)
{
    Scene scene;
    scene.parts = {part(ComponentKind::Resistor, "R1", {0, 0}),
                   part(ComponentKind::Capacitor, "C1", {1, 0})};
    scene.wires = {{{10, 0}, {10, 8}}};
    scene.labels = {{"out", {10, 4}}};
    CHECK(partAt(scene, {-2, 0}) == std::optional<std::size_t>(0));
    CHECK(partAt(scene, {1, 1}) == std::optional<std::size_t>(1)); // both cover it: C1 is on top
    CHECK(!partAt(scene, {5, 5}));
    CHECK(wireAt(scene, {10, 5}) == std::optional<std::size_t>(0));
    CHECK(!wireAt(scene, {11, 5}));
    CHECK(labelAt(scene, {10, 4}) == std::optional<std::size_t>(0));
}

TEST(new_parts_take_the_next_designator_of_their_letter)
{
    Scene scene;
    CHECK_EQ(nextDesignator(scene, ComponentKind::Resistor), std::string("R1"));
    scene.parts = {part(ComponentKind::Resistor, "R1", {0, 0}),
                   part(ComponentKind::Resistor, "r7", {0, 4}),
                   part(ComponentKind::Resistor, "Rload", {0, 8}),
                   part(ComponentKind::Capacitor, "C2", {0, 12})};
    CHECK_EQ(nextDesignator(scene, ComponentKind::Resistor), std::string("R8"));
    CHECK_EQ(nextDesignator(scene, ComponentKind::Capacitor), std::string("C3"));
    CHECK_EQ(nextDesignator(scene, ComponentKind::VoltageSource), std::string("V1"));
    CHECK(nextDesignator(scene, ComponentKind::Ground).empty());
}

TEST(wires_route_horizontally_then_vertically)
{
    CHECK(route({0, 0}, {0, 0}).empty());
    CHECK(route({0, 0}, {5, 0}) == (std::vector<Wire>{{{0, 0}, {5, 0}}}));
    CHECK(route({0, 0}, {5, 3}) == (std::vector<Wire>{{{0, 0}, {5, 0}}, {{5, 0}, {5, 3}}}));
}

TEST(junction_dots_mark_three_way_connections)
{
    Scene scene;
    scene.wires = {{{0, 0}, {10, 0}}, {{5, 0}, {5, 5}}, {{20, 0}, {30, 0}}, {{25, -5}, {25, 5}}};
    // The T at (5, 0) gets a dot; the plain crossing at (25, 0) does not.
    CHECK(junctionDots(scene) == (std::vector<Point>{{5, 0}}));
    scene.junctions = {{25, 0}};
    CHECK(junctionDots(scene) == (std::vector<Point>{{5, 0}, {25, 0}}));
}

TEST(moving_and_turning_a_part_keeps_its_wires_on_its_pins)
{
    Scene scene;
    scene.parts = {part(ComponentKind::Resistor, "R1", {0, 0})}; // pins (-2,0), (2,0)
    scene.wires = {{{-6, 0}, {-2, 0}}, {{2, 0}, {6, 0}}};
    movePart(scene, 0, {0, 3});
    CHECK(scene.wires[0] == (Wire{{-6, 0}, {-2, 3}}));
    CHECK(scene.wires[1] == (Wire{{2, 3}, {6, 0}}));
    rotatePart(scene, 0); // pins now (0, 1) and (0, 5)
    CHECK(scene.parts[0].placement.rotation == Rotation::R90);
    CHECK(scene.wires[0].to == (Point{0, 1}));
    CHECK(scene.wires[1].from == (Point{0, 5}));
}

TEST(remove_drops_the_chosen_items)
{
    Scene scene;
    scene.parts = {part(ComponentKind::Resistor, "R1", {0, 0}),
                   part(ComponentKind::Resistor, "R2", {0, 4}),
                   part(ComponentKind::Resistor, "R3", {0, 8})};
    scene.wires = {{{0, 0}, {1, 0}}, {{0, 1}, {1, 1}}};
    scene.labels = {{"a", {0, 0}}};
    remove(scene, {2, 0, 2}, {1}, {0});
    CHECK_EQ(scene.parts.size(), 1u);
    CHECK_EQ(scene.parts[0].designator, std::string("R2"));
    CHECK_EQ(scene.wires.size(), 1u);
    CHECK(scene.labels.empty());
}

// The Done-when of M4.2, headless: the voltage divider drawn with the editor's operations (place,
// route a wire between two clicks, label) gives the built-in divider's netlist.
TEST(a_divider_drawn_by_hand_gives_the_divider_netlist)
{
    Scene scene;
    const auto placeNew = [&](ComponentKind kind, Point at, Rotation rotation = Rotation::R0) {
        PlacedPart p = part(kind, nextDesignator(scene, kind), at, rotation);
        if (kind == ComponentKind::Resistor) {
            p.value = "10k";
        }
        scene.parts.push_back(p);
    };
    placeNew(ComponentKind::VoltageSource, {0, 2});           // V1, pins (0,0), (0,4)
    placeNew(ComponentKind::Resistor, {4, 0});                // R1, pins (2,0), (6,0)
    placeNew(ComponentKind::Resistor, {8, 2}, Rotation::R90); // R2, pins (8,0), (8,4)
    placeNew(ComponentKind::Ground, {4, 6});                  // pin (4,6)
    addRoute(scene, {0, 0}, {2, 0});
    addRoute(scene, {6, 0}, {8, 0});
    addRoute(scene, {0, 4}, {0, 6}); // V1's - down
    addRoute(scene, {0, 6}, {8, 6}); // along the bottom, over the ground pin at (4, 6)
    addRoute(scene, {8, 4}, {8, 6}); // R2 down to it
    scene.labels = {{"in", {1, 0}}, {"out", {7, 0}}};
    const Conversion conversion = convert(scene);
    CHECK(conversion.diagnostics.empty());
    CHECK(validate(conversion.circuit).empty());
    CHECK_EQ(buildNetlist(conversion.circuit),
             std::string("* Proton netlist\nR1 in out 10k\nR2 out 0 10k\nV1 in 0 DC 5\n"));
}
