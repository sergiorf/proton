// SPDX-License-Identifier: AGPL-3.0-only
// Wiring as a desktop circuit simulator does it (PLAN.md M4.12): routes, moves with the wires
// routed again, dragged segments and wire ends, taps and tidying.
#include "proton/schematic/scene.h"
#include "proton/schematic/scene_edit.h"
#include "proton/test/test.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

using namespace proton::schematic;

namespace {

PlacedPart part(ComponentKind kind, std::string designator, Point at,
                Rotation rotation = Rotation::R0)
{
    return {kind, std::move(designator), defaultValue(kind), {at, rotation, false}};
}

// Every wire horizontal or vertical.
bool square(const Scene& scene)
{
    return std::ranges::all_of(scene.wires, [](const Wire& wire) {
        return wire.from.x == wire.to.x || wire.from.y == wire.to.y;
    });
}

// The wires form one path from `from` to `to`, end to end.
bool path(const std::vector<Wire>& wires, Point from, Point to)
{
    Point at = from;
    for (const Wire& wire : wires) {
        if (wire.from != at) {
            return false;
        }
        at = wire.to;
    }
    return at == to;
}

// Whether `p` lies on one of `wires`.
bool touches(const std::vector<Wire>& wires, Point p)
{
    return std::ranges::any_of(wires, [p](const Wire& wire) {
        return p.x >= std::min(wire.from.x, wire.to.x) && p.x <= std::max(wire.from.x, wire.to.x) &&
               p.y >= std::min(wire.from.y, wire.to.y) && p.y <= std::max(wire.from.y, wire.to.y);
    });
}

// The 5 V divider: V1 at the left, R1 across the top, R2 down the right, ground on the bottom
// wire.
//
//     (0,0)-[R1]-(8,0)
//       V1         R2
//     (0,4)        (8,4)
//       \___(4,6)___/  with ground at (4,6)
Scene divider()
{
    Scene scene;
    scene.parts = {
        part(ComponentKind::VoltageSource, "V1", {0, 2}),         // (0,0), (0,4)
        part(ComponentKind::Resistor, "R1", {4, 0}),              // (2,0), (6,0)
        part(ComponentKind::Resistor, "R2", {8, 2}, Rotation::R90), // (8,0), (8,4)
        part(ComponentKind::Ground, "", {4, 6}),
    };
    scene.wires = {{{0, 0}, {2, 0}}, {{6, 0}, {8, 0}}, {{0, 4}, {0, 6}}, {{0, 6}, {8, 6}},
                   {{8, 4}, {8, 6}}};
    return scene;
}

// Whether moving the one item of `selection` by `offset` puts one of its pins on another item's
// pin or on a wire not attached to it: a new connection the student made, not the router.
bool landsOnDrawing(const Scene& scene, const Selection& selection, Point offset)
{
    Scene rest = scene;
    std::vector<Point> was;
    if (!selection.parts.empty()) {
        const PlacedPart& moved = scene.parts[selection.parts[0]];
        for (const Point p : pinOffsets(moved.kind)) {
            was.push_back(place(p, moved.placement));
        }
        rest.parts.erase(rest.parts.begin() + static_cast<std::ptrdiff_t>(selection.parts[0]));
    } else {
        const PlacedInstrument& moved = scene.instruments[selection.instruments[0]];
        for (const Point p : terminalOffsets(moved.kind)) {
            was.push_back(place(p, moved.placement));
        }
        rest.instruments.erase(rest.instruments.begin() +
                               static_cast<std::ptrdiff_t>(selection.instruments[0]));
    }
    std::erase_if(rest.wires, [&was](const Wire& wire) {
        return std::ranges::find(was, wire.from) != was.end() ||
               std::ranges::find(was, wire.to) != was.end();
    });
    return std::ranges::any_of(was, [&](Point p) {
        const Point now{p.x + offset.x, p.y + offset.y};
        return pinAt(rest, now) || touches(rest.wires, now);
    });
}

std::string netOf(const Conversion& conversion, std::size_t part, std::size_t pin)
{
    return conversion.circuit.components[part].nets[pin];
}

} // namespace

TEST(a_route_with_nothing_in_its_way_is_an_l)
{
    const Scene empty;
    const std::vector<Wire> wires = routeAround(empty, {0, 0}, {5, 3});
    CHECK_EQ(wires.size(), 2u);
    CHECK(path(wires, {0, 0}, {5, 3}));
    CHECK(routeAround(empty, {1, 1}, {1, 1}).empty());
}

// R1 lies across the straight line: the route goes round it, touching neither of its pins and
// staying out of its box.
TEST(a_route_goes_round_a_symbol)
{
    Scene scene;
    scene.parts = {part(ComponentKind::Resistor, "R1", {5, 0})}; // pins (3,0), (7,0)
    const std::vector<Wire> wires = routeAround(scene, {0, 0}, {10, 0});
    CHECK(path(wires, {0, 0}, {10, 0}));
    CHECK(!touches(wires, {3, 0}));
    CHECK(!touches(wires, {7, 0}));
    CHECK(!touches(wires, {5, 0}));
    Scene drawn = scene;
    drawn.wires = wires;
    CHECK(square(drawn));
}

// A crossing does not connect, so a route may cross a wire straight; it never bends on it.
TEST(a_route_crosses_a_wire_but_never_bends_on_it)
{
    Scene scene;
    scene.wires = {{{5, -5}, {5, 5}}};
    CHECK(routeAround(scene, {0, 0}, {10, 0}) == (std::vector<Wire>{{{0, 0}, {10, 0}}}));
    const std::vector<Wire> wires = routeAround(scene, {0, 0}, {5, 8});
    CHECK(path(wires, {0, 0}, {5, 8}));
    for (std::size_t i = 1; i < wires.size(); ++i) {
        CHECK(!(wires[i].from.x == 5 && wires[i].from.y >= -5 && wires[i].from.y <= 5));
    }
}

TEST(moving_a_part_routes_its_wires_square_and_keeps_its_nets)
{
    const Scene before = divider();
    for (const Point offset : {Point{3, 2}, Point{-2, 5}, Point{6, -4}}) {
        Scene scene = before;
        Selection r2;
        r2.parts = {2};
        moveSelection(scene, r2, offset);
        CHECK(square(scene));
        CHECK(sameNets(before, scene));
        CHECK_EQ(scene.parts[2].placement.position.x, 8 + offset.x);
    }
}

TEST(turning_and_mirroring_route_the_wires_square_and_keep_the_nets)
{
    const Scene before = divider();
    Selection r1;
    r1.parts = {1};
    Scene turned = before;
    rotateSelection(turned, r1);
    CHECK(turned.parts[1].placement.rotation == Rotation::R90);
    CHECK(square(turned));
    CHECK(sameNets(before, turned));
    Selection v1;
    v1.parts = {0};
    Scene mirrored = before;
    mirrorSelection(mirrored, v1);
    CHECK(square(mirrored));
    CHECK(sameNets(before, mirrored));
}

// Everything selected moves as it is: no wire is routed again. (The bottom wire comes back in two
// pieces, split at the ground pin on its middle.)
TEST(moving_everything_moves_the_wires_with_it)
{
    Scene scene = divider();
    const Scene before = scene;
    Selection all;
    all.parts = {0, 1, 2, 3};
    all.wires = {0, 1, 2, 3, 4};
    const Selection moved = moveSelection(scene, all, {5, 5});
    std::vector<Wire> shifted;
    for (const Wire& wire : before.wires) {
        shifted.push_back({{wire.from.x + 5, wire.from.y + 5}, {wire.to.x + 5, wire.to.y + 5}});
    }
    for (const Wire& wire : scene.wires) {
        CHECK(std::ranges::any_of(shifted, [&wire](const Wire& original) {
            return touches({original}, wire.from) && touches({original}, wire.to) &&
                   (original.from.x == original.to.x) == (wire.from.x == wire.to.x);
        }));
    }
    CHECK_EQ(moved.wires.size(), scene.wires.size()); // every piece still selected
    CHECK(sameNets(before, scene));
}

// A pin sitting on another part's pin stays joined to it when its part moves away.
TEST(a_pin_that_touched_another_pin_stays_connected)
{
    Scene scene;
    scene.parts = {part(ComponentKind::Resistor, "R1", {0, 0}),  // (-2,0), (2,0)
                   part(ComponentKind::Resistor, "R2", {4, 0})}; // (2,0), (6,0)
    const Scene before = scene;
    Selection r2;
    r2.parts = {1};
    moveSelection(scene, r2, {3, 4});
    CHECK(sameNets(before, scene));
    CHECK(square(scene));
}

// The bottom wire dragged down: the two vertical wires stretch, and the ground pin on its middle
// is joined to it by a stub.
TEST(dragging_a_segment_stretches_its_neighbours)
{
    Scene scene = divider();
    const Scene before = scene;
    dragSegment(scene, 3, {7, 2}); // horizontal: only down by 2
    CHECK(scene.wires[3] == (Wire{{0, 8}, {8, 8}}));
    CHECK(scene.wires[2] == (Wire{{0, 4}, {0, 8}}));
    CHECK(scene.wires[4] == (Wire{{8, 4}, {8, 8}}));
    CHECK(square(scene));
    CHECK(sameNets(before, scene));
}

TEST(dragging_a_segment_whose_end_is_a_pin_adds_a_stub)
{
    Scene scene = divider();
    const Scene before = scene;
    dragSegment(scene, 0, {0, -3}); // V1 + to R1: its ends are pins
    CHECK(scene.wires[0] == (Wire{{0, -3}, {2, -3}}));
    CHECK(square(scene));
    CHECK(sameNets(before, scene));
}

// The probe lead moved: scope1's CH1 runs from R1's right pin, and its end there is dragged to
// R1's left pin; dragged into empty sheet, it is disconnected.
TEST(dragging_a_wire_end_moves_it_to_another_net_or_off_it)
{
    Scene scene = divider();
    scene.instruments = {{InstrumentKind::Scope, "scope1", {{20, -1}, Rotation::R0, false}}};
    // CH1 is at (17,-2): over to (6,-2), then down to R1's right pin.
    scene.wires.push_back({{17, -2}, {6, -2}});
    scene.wires.push_back({{6, -2}, {6, 0}});
    const std::size_t lead = scene.wires.size() - 1;
    CHECK_EQ(convert(scene).instruments[0].nets[0], netOf(convert(scene), 1, 1));

    Scene moved = scene;
    dragWireEnd(moved, lead, {6, 0}, {2, 0});
    CHECK(square(moved));
    const Conversion after = convert(moved);
    CHECK_EQ(after.instruments[0].nets[0], netOf(after, 1, 0));
    CHECK(after.instruments[0].nets[0] != netOf(after, 1, 1));

    Scene off = scene;
    dragWireEnd(off, lead, {6, 0}, {12, -8});
    CHECK(convert(off).instruments[0].nets[0].empty()); // not connected
}

// A press grabs a wire by its end where that end is the only thing wired there, a terminal or pin
// included: the scope's CH1 lead is moved off CH1 onto CH2 without redrawing it.
TEST(a_press_grabs_the_only_wire_end_at_a_point_on_a_pin_or_terminal_too)
{
    Scene scene = divider();
    scene.instruments = {{InstrumentKind::Scope, "scope1", {{20, -1}, Rotation::R0, false}}};
    // CH1 at (17,-2) to R1's right pin (6,0), bending at (6,-2); CH2 is at (17,0).
    scene.wires.push_back({{17, -2}, {6, -2}});
    const std::size_t lead = scene.wires.size() - 1;
    scene.wires.push_back({{6, -2}, {6, 0}});
    CHECK(wireEndAt(scene, {17, -2}) == lead);  // on a terminal
    CHECK(!wireEndAt(scene, {6, 0}));           // R1's pin, where two wires end
    CHECK(!wireEndAt(scene, {6, -2}));          // a bend: two wire ends
    CHECK(!wireEndAt(scene, {10, -2}));         // a wire's middle
    CHECK(!wireEndAt(scene, {12, 12}));         // nothing

    dragWireEnd(scene, lead, {17, -2}, {17, 0});
    const Conversion after = convert(scene);
    CHECK(after.instruments[0].nets[0].empty()); // CH1 not connected
    CHECK_EQ(after.instruments[0].nets[1], netOf(after, 1, 1));

    // A free end, unless a label, a junction or another wire is there.
    Scene free;
    free.wires = {{{0, 0}, {4, 0}}};
    CHECK(wireEndAt(free, {4, 0}) == std::size_t{0});
    free.labels = {{"out", {4, 0}}};
    CHECK(!wireEndAt(free, {4, 0}));
    free.labels.clear();
    free.junctions = {{4, 0}};
    CHECK(!wireEndAt(free, {4, 0}));
    free.junctions.clear();
    free.wires.push_back({{4, -2}, {4, 2}});
    CHECK(!wireEndAt(free, {4, 0}));
}

// A wire across a symbol's inside: straight through R1 from pin to pin, which shorts it, or across
// its body or the scope's; one along the edge or ending on a pin does not cross.
TEST(a_wire_crosses_a_symbol_through_its_inside_only)
{
    Scene scene = divider();
    scene.instruments = {{InstrumentKind::Scope, "scope1", {{20, -1}, Rotation::R0, false}}};
    CHECK(std::ranges::none_of(scene.wires,
                               [&](const Wire& wire) { return crossesSymbol(scene, wire); }));
    CHECK(crossesSymbol(scene, {{2, 0}, {6, 0}}));   // R1 pin to pin
    CHECK(crossesSymbol(scene, {{4, -5}, {4, 5}}));  // across R1's body
    CHECK(!crossesSymbol(scene, {{4, -5}, {4, -1}})); // up to its edge
    CHECK(!crossesSymbol(scene, {{0, -1}, {7, -1}})); // along its edge
    const Box box = bounds(scene.instruments[0]);
    const int middle = (box.top + box.bottom) / 2;
    CHECK(crossesSymbol(scene, {{box.left - 2, middle}, {box.right + 2, middle}}));
    CHECK(!crossesSymbol(scene, {{box.left - 2, middle}, {box.left, middle}}));
}

// The Wire tool draws what was clicked, over pins and wires to join them, but not through a
// symbol.
TEST(the_wire_tool_draws_straight_but_round_a_symbol)
{
    const Scene scene = divider();
    // Along the bottom wire, over ground's pin: as clicked.
    CHECK(wireToolRoute(scene, {-2, 6}, {10, 6}) == route({-2, 6}, {10, 6}));
    CHECK(wireToolRoute(scene, {10, -4}, {12, 2}) == route({10, -4}, {12, 2}));
    // Down through R1's body: round it.
    const std::vector<Wire> round = wireToolRoute(scene, {4, -4}, {4, 3});
    CHECK(path(round, {4, -4}, {4, 3}));
    CHECK(std::ranges::none_of(round, [&](const Wire& wire) { return crossesSymbol(scene, wire); }));
}

TEST(a_wire_end_dragged_onto_a_wires_middle_taps_it)
{
    Scene scene;
    scene.wires = {{{0, 0}, {10, 0}}, {{20, 5}, {20, 10}}};
    dragWireEnd(scene, 1, {20, 5}, {4, 0});
    CHECK(std::ranges::find(scene.wires, Wire{{0, 0}, {4, 0}}) != scene.wires.end());
    CHECK(std::ranges::find(scene.wires, Wire{{4, 0}, {10, 0}}) != scene.wires.end());
    const std::vector<Point> dots = junctionDots(scene);
    CHECK(std::ranges::find(dots, Point{4, 0}) != dots.end());
}

TEST(tapping_splits_a_wire_only_in_its_middle)
{
    Scene scene;
    scene.wires = {{{0, 0}, {10, 0}}};
    CHECK(!tapWire(scene, {0, 0}));
    CHECK(!tapWire(scene, {5, 1}));
    CHECK(tapWire(scene, {4, 0}));
    CHECK(scene.wires == (std::vector<Wire>{{{0, 0}, {4, 0}}, {{4, 0}, {10, 0}}}));
}

TEST(tidying_merges_straight_continuations_and_drops_empty_and_double_wires)
{
    Scene scene;
    scene.wires = {{{0, 0}, {2, 0}}, {{2, 0}, {5, 0}}, {{5, 0}, {5, 3}}, {{7, 7}, {7, 7}},
                   {{5, 3}, {5, 0}}};
    tidyWires(scene);
    CHECK(scene.wires == (std::vector<Wire>{{{0, 0}, {5, 0}}, {{5, 0}, {5, 3}}}));

    // Not where a pin is, nor where a third wire ends.
    Scene pinned;
    pinned.parts = {part(ComponentKind::Ground, "", {2, 0})};
    pinned.wires = {{{0, 0}, {2, 0}}, {{2, 0}, {5, 0}}};
    tidyWires(pinned);
    CHECK_EQ(pinned.wires.size(), 2u);
    Scene branch;
    branch.wires = {{{0, 0}, {2, 0}}, {{2, 0}, {5, 0}}, {{2, 0}, {2, 4}}};
    tidyWires(branch);
    CHECK_EQ(branch.wires.size(), 3u);
}

TEST(same_nets_sees_a_new_connection_but_not_a_new_route)
{
    const Scene before = divider();
    Scene rerouted = before;
    rerouted.wires[3] = {{0, 6}, {0, 9}};
    rerouted.wires.push_back({{0, 9}, {8, 9}});
    rerouted.wires.push_back({{8, 9}, {8, 6}});
    rerouted.wires.push_back({{4, 6}, {4, 9}}); // ground's pin, joined from below
    CHECK(sameNets(before, rerouted));
    Scene shorted = before;
    shorted.wires.push_back({{2, 0}, {2, 6}}); // R1's left pin to ground
    CHECK(!sameNets(before, shorted));
}

// The plan's check over many edits: every part and instrument of the divider with a scope wired
// to it, moved by every offset of a grid around it, and turned and mirrored, keeps every wire
// square and every net as it was.
TEST(any_move_or_turn_keeps_the_wires_square_and_the_nets)
{
    Scene start = divider();
    start.instruments = {{InstrumentKind::Scope, "scope1", {{20, -1}, Rotation::R0, false}}};
    start.wires.push_back({{17, -2}, {6, -2}});
    start.wires.push_back({{6, -2}, {6, 0}});
    start.wires.push_back({{17, 0}, {17, 6}});
    start.wires.push_back({{17, 6}, {8, 6}});
    int checked = 0;
    for (std::size_t item = 0; item < start.parts.size() + start.instruments.size(); ++item) {
        Selection one;
        if (item < start.parts.size()) {
            one.parts = {item};
        } else {
            one.instruments = {item - start.parts.size()};
        }
        for (int dx = -9; dx <= 9; dx += 3) {
            for (int dy = -9; dy <= 9; dy += 3) {
                if (landsOnDrawing(start, one, {dx, dy})) {
                    continue; // a pin put on another net joins it, as it should
                }
                Scene scene = start;
                moveSelection(scene, one, {dx, dy});
                tidyWires(scene);
                CHECK(square(scene));
                CHECK(sameNets(start, scene));
                ++checked;
            }
        }
        Scene turned = start;
        rotateSelection(turned, one);
        CHECK(square(turned));
        CHECK(sameNets(start, turned));
        Scene mirrored = start;
        mirrorSelection(mirrored, one);
        CHECK(square(mirrored));
        CHECK(sameNets(start, mirrored));
    }
    CHECK(checked > 5 * 49 * 3 / 4); // most of the moves were checked
}
