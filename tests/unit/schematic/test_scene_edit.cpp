// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/netlist.h"
#include "proton/schematic/scene.h"
#include "proton/schematic/scene_edit.h"
#include "proton/schematic/validate.h"
#include "proton/test/test.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace proton::schematic;

namespace {

PlacedPart part(ComponentKind kind, std::string designator, Point at,
                Rotation rotation = Rotation::R0)
{
    return {kind, std::move(designator), defaultValue(kind), {at, rotation, false}};
}

PlacedInstrument instrument(InstrumentKind kind, std::string name, Point at,
                            Rotation rotation = Rotation::R0)
{
    return {kind, std::move(name), {at, rotation, false}};
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

TEST(instrument_bounds_have_the_terminals_on_their_edge)
{
    // Generator: terminals on the right edge; scope: on the left; multimeter: along the bottom.
    CHECK(bounds(instrument(InstrumentKind::Generator, "gen1", {0, 0})) == (Box{-4, -2, 2, 2}));
    CHECK(bounds(instrument(InstrumentKind::Scope, "scope1", {0, 0})) == (Box{-3, -3, 4, 3}));
    CHECK(bounds(instrument(InstrumentKind::Multimeter, "dmm1", {0, 0})) == (Box{-2, -3, 2, 2}));
    const PlacedInstrument turned =
        instrument(InstrumentKind::Multimeter, "dmm1", {10, 10}, Rotation::R90);
    CHECK(bounds(turned) == (Box{8, 8, 13, 12}));
    for (const InstrumentKind kind :
         {InstrumentKind::Generator, InstrumentKind::Scope, InstrumentKind::Multimeter}) {
        const PlacedInstrument placed = instrument(kind, "x", {3, 5}, Rotation::R270);
        for (const Point offset : terminalOffsets(kind)) {
            const Point p = place(offset, placed.placement);
            const Box box = bounds(placed);
            CHECK(box.contains(p));
            CHECK(p.x == box.left || p.x == box.right || p.y == box.top || p.y == box.bottom);
        }
    }
}

TEST(hit_testing_finds_the_topmost_instrument)
{
    Scene scene;
    scene.instruments = {instrument(InstrumentKind::Scope, "scope1", {0, 0}),
                         instrument(InstrumentKind::Multimeter, "dmm1", {3, 0})};
    CHECK(instrumentAt(scene, {-3, 0}) == std::optional<std::size_t>(0));
    CHECK(instrumentAt(scene, {2, 1}) == std::optional<std::size_t>(1)); // both: dmm1 on top
    CHECK(!instrumentAt(scene, {20, 0}));
}

TEST(new_instruments_take_the_next_name_of_their_kind)
{
    Scene scene;
    CHECK_EQ(nextInstrumentName(scene, InstrumentKind::Scope), std::string("scope1"));
    CHECK_EQ(nextInstrumentName(scene, InstrumentKind::Generator), std::string("gen1"));
    CHECK_EQ(nextInstrumentName(scene, InstrumentKind::Multimeter), std::string("dmm1"));
    scene.instruments = {instrument(InstrumentKind::Scope, "scope1", {0, 0}),
                         instrument(InstrumentKind::Scope, "SCOPE3", {10, 0}),
                         instrument(InstrumentKind::Multimeter, "dmm2", {20, 0})};
    CHECK_EQ(nextInstrumentName(scene, InstrumentKind::Scope), std::string("scope4"));
    CHECK_EQ(nextInstrumentName(scene, InstrumentKind::Multimeter), std::string("dmm3"));
    CHECK_EQ(nextInstrumentName(scene, InstrumentKind::Generator), std::string("gen1"));
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

TEST(moving_and_turning_an_instrument_keeps_its_wires_on_its_terminals)
{
    Scene scene;
    scene.instruments = {instrument(InstrumentKind::Scope, "scope1", {0, 0})}; // (-3,-1), (-3,1)
    scene.wires = {{{-8, -1}, {-3, -1}}, {{-3, 1}, {-8, 1}}};
    moveInstrument(scene, 0, {2, 2});
    CHECK(scene.instruments[0].placement.position == (Point{2, 2}));
    CHECK(scene.wires[0] == (Wire{{-8, -1}, {-1, 1}}));
    CHECK(scene.wires[1] == (Wire{{-1, 3}, {-8, 1}}));
    rotateInstrument(scene, 0); // terminals now (3, -1) and (1, -1)
    CHECK(scene.instruments[0].placement.rotation == Rotation::R90);
    CHECK(scene.wires[0].to == (Point{3, -1}));
    CHECK(scene.wires[1].from == (Point{1, -1}));
}

TEST(junction_dots_count_instrument_terminals)
{
    Scene scene;
    scene.instruments = {instrument(InstrumentKind::Scope, "scope1", {0, 0})}; // CH1 at (-3,-1)
    scene.wires = {{{-6, -1}, {-3, -1}}};
    CHECK(junctionDots(scene).empty()); // a wire ending on a terminal is two connections
    scene.wires.push_back({{-3, -1}, {-3, -5}});
    CHECK(junctionDots(scene) == (std::vector<Point>{{-3, -1}}));
}

TEST(remove_drops_the_chosen_items)
{
    Scene scene;
    scene.parts = {part(ComponentKind::Resistor, "R1", {0, 0}),
                   part(ComponentKind::Resistor, "R2", {0, 4}),
                   part(ComponentKind::Resistor, "R3", {0, 8})};
    scene.wires = {{{0, 0}, {1, 0}}, {{0, 1}, {1, 1}}};
    scene.labels = {{"a", {0, 0}}};
    scene.instruments = {instrument(InstrumentKind::Scope, "scope1", {20, 0}),
                         instrument(InstrumentKind::Multimeter, "dmm1", {30, 0})};
    remove(scene, {2, 0, 2}, {1}, {0}, {0});
    CHECK_EQ(scene.parts.size(), 1u);
    CHECK_EQ(scene.parts[0].designator, std::string("R2"));
    CHECK_EQ(scene.wires.size(), 1u);
    CHECK(scene.labels.empty());
    CHECK_EQ(scene.instruments.size(), 1u);
    CHECK_EQ(scene.instruments[0].name, std::string("dmm1"));
}

TEST(remove_drops_junctions_too)
{
    Scene scene;
    scene.junctions = {{0, 0}, {4, 0}};
    CHECK(junctionAt(scene, {4, 0}) == std::optional<std::size_t>(1));
    CHECK(!junctionAt(scene, {2, 0}));
    remove(scene, {}, {}, {}, {}, {0});
    CHECK(scene.junctions == (std::vector<Point>{{4, 0}}));
}

// Mirroring flips the pins left to right on screen about the position, however the item is
// turned, and twice gives the item back.
TEST(mirroring_flips_pins_left_to_right_and_wires_follow)
{
    const auto flipped = [](Point p, Point about) { return Point{2 * about.x - p.x, p.y}; };
    for (int r = 0; r < 4; ++r) {
        const auto rotation = static_cast<Rotation>(r);
        Scene scene;
        scene.parts = {part(ComponentKind::VoltageSource, "V1", {3, 2}, rotation)};
        scene.instruments = {instrument(InstrumentKind::Generator, "gen1", {-10, 4}, rotation)};
        const PlacedPart partBefore = scene.parts[0];
        const PlacedInstrument instrumentBefore = scene.instruments[0];
        const Point pin = place(pinOffsets(ComponentKind::VoltageSource)[0], partBefore.placement);
        const Point terminal = place(terminalOffsets(InstrumentKind::Generator)[0],
                                     instrumentBefore.placement);
        scene.wires = {{pin, {20, 20}}, {terminal, {-20, 20}}};

        mirrorPart(scene, 0);
        mirrorInstrument(scene, 0);
        for (std::size_t i = 0; i < 2; ++i) {
            CHECK(place(pinOffsets(ComponentKind::VoltageSource)[i], scene.parts[0].placement) ==
                  flipped(place(pinOffsets(ComponentKind::VoltageSource)[i], partBefore.placement),
                          {3, 2}));
            CHECK(place(terminalOffsets(InstrumentKind::Generator)[i],
                        scene.instruments[0].placement) ==
                  flipped(place(terminalOffsets(InstrumentKind::Generator)[i],
                                instrumentBefore.placement),
                          {-10, 4}));
        }
        CHECK(scene.wires[0].from == flipped(pin, {3, 2}));
        CHECK(scene.wires[1].from == flipped(terminal, {-10, 4}));

        mirrorPart(scene, 0);
        mirrorInstrument(scene, 0);
        CHECK(scene.parts[0] == partBefore);
        CHECK(scene.instruments[0] == instrumentBefore);
    }
}

TEST(scene_bounds_cover_everything_drawn)
{
    Scene scene;
    CHECK(!bounds(scene));
    scene.parts = {part(ComponentKind::Resistor, "R1", {0, 0})}; // -2..2, -1..1
    scene.wires = {{{2, 0}, {2, 9}}};
    scene.labels = {{"out", {-5, 3}}};
    scene.junctions = {{6, -4}};
    CHECK(bounds(scene) == (Box{-5, -4, 6, 9}));
}

TEST(copy_and_paste_give_new_names_and_keep_labels)
{
    Scene scene;
    scene.parts = {part(ComponentKind::Resistor, "R1", {0, 0}),
                   part(ComponentKind::Resistor, "R2", {0, 4}),
                   part(ComponentKind::Ground, "", {0, 8})};
    scene.instruments = {instrument(InstrumentKind::Multimeter, "dmm1", {10, 0})};
    scene.wires = {{{2, 0}, {2, 4}}, {{-2, 0}, {-2, 4}}};
    scene.labels = {{"out", {2, 2}}};
    scene.junctions = {{2, 2}};

    Selection selection;
    selection.parts = {2, 0}; // in scene order on the clipboard, whatever the selection's order
    selection.wires = {0};
    selection.labels = {0};
    selection.instruments = {0};
    selection.junctions = {0};
    const Scene clip = extract(scene, selection);
    CHECK_EQ(clip.parts.size(), 2u);
    CHECK_EQ(clip.parts[0].designator, std::string("R1"));
    CHECK(clip.wires == (std::vector<Wire>{{{2, 0}, {2, 4}}}));

    const Scene before = scene;
    const Selection added = paste(scene, clip, {20, 0});
    CHECK(added.parts == (std::vector<std::size_t>{3, 4}));
    CHECK(added.wires == (std::vector<std::size_t>{2}));
    CHECK(added.labels == (std::vector<std::size_t>{1}));
    CHECK(added.instruments == (std::vector<std::size_t>{1}));
    CHECK(added.junctions == (std::vector<std::size_t>{1}));
    CHECK_EQ(scene.parts[3].designator, std::string("R3")); // R1 and R2 are taken
    CHECK_EQ(scene.parts[3].value, std::string("1k"));
    CHECK(scene.parts[3].placement.position == (Point{20, 0}));
    CHECK(scene.parts[4].designator.empty()); // ground has none
    CHECK_EQ(scene.instruments[1].name, std::string("dmm2"));
    CHECK(scene.instruments[1].placement.position == (Point{30, 0}));
    CHECK(scene.wires[2] == (Wire{{22, 0}, {22, 4}}));
    CHECK(scene.labels[1] == (NetLabel{"out", {22, 2}})); // the same net name
    CHECK(scene.junctions[1] == (Point{22, 2}));
    CHECK(std::equal(before.parts.begin(), before.parts.end(), scene.parts.begin()));

    // Pasted again: the next names on.
    paste(scene, clip, {40, 0});
    CHECK_EQ(scene.parts[5].designator, std::string("R4"));
    CHECK_EQ(scene.instruments[2].name, std::string("dmm3"));
    CHECK(paste(scene, Scene{}, {1, 1}).empty());
}

TEST(a_box_selects_what_lies_wholly_inside_it)
{
    Scene scene;
    scene.parts = {part(ComponentKind::Resistor, "R1", {10, 10}),  // 8..12 x 9..11
                   part(ComponentKind::Resistor, "R2", {20, 10})}; // 18..22 x 9..11
    scene.wires = {{{0, 0}, {4, 0}}, {{12, 10}, {18, 10}}};
    scene.labels = {{"in", {2, 0}}, {"out", {16, 10}}};
    scene.junctions = {{3, 0}};
    scene.instruments = {instrument(InstrumentKind::Scope, "scope1", {10, 30})};

    // R1, the wire, label and junction on the left; not R2, nor the wire half inside, nor `out`.
    Selection selected = within(scene, {-1, -1, 13, 12});
    CHECK(selected.parts == std::vector<std::size_t>{0});
    CHECK(selected.wires == std::vector<std::size_t>{0});
    CHECK(selected.labels == std::vector<std::size_t>{0});
    CHECK(selected.junctions == std::vector<std::size_t>{0});
    CHECK(selected.instruments.empty());

    // Edges are inside: a box exactly round a symbol selects it, one a unit smaller does not.
    const Box scope = bounds(scene.instruments[0]);
    CHECK(within(scene, scope).instruments == std::vector<std::size_t>{0});
    CHECK(within(scene, {scope.left + 1, scope.top, scope.right, scope.bottom}).empty());
    CHECK(within(scene, bounds(scene.parts[1])).parts == std::vector<std::size_t>{1});

    // A box round everything selects everything.
    const Selection all = within(scene, {-100, -100, 100, 100});
    CHECK(all.parts.size() == 2 && all.wires.size() == 2 && all.labels.size() == 2 &&
          all.junctions.size() == 1 && all.instruments.size() == 1);
}

TEST(instrument_names_are_taken_ignoring_case)
{
    Scene scene;
    scene.instruments = {instrument(InstrumentKind::Scope, "scope1", {0, 0}),
                         instrument(InstrumentKind::Multimeter, "dmm1", {10, 0})};
    CHECK(instrumentNameTaken(scene, "SCOPE1"));
    CHECK(!instrumentNameTaken(scene, "scope1", 0)); // its own name
    CHECK(instrumentNameTaken(scene, "dmm1", 0));
    CHECK(!instrumentNameTaken(scene, "probe"));
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

// The Done-when of M4.4, headless: every terminal of the scope, multimeter and generator is
// connected by drawing wires to the circuit, with no label at them, and gets its net.
TEST(instruments_wired_by_hand_are_on_the_nets_they_touch)
{
    Scene scene;
    scene.parts = {part(ComponentKind::Resistor, "R1", {4, 0}),               // (2,0), (6,0)
                   part(ComponentKind::Resistor, "R2", {8, 2}, Rotation::R90), // (8,0), (8,4)
                   part(ComponentKind::Ground, "", {8, 6})};
    addRoute(scene, {6, 0}, {8, 0});
    addRoute(scene, {8, 4}, {8, 6});
    scene.labels = {{"out", {7, 0}}};
    // gen1 drives R1: + at (-1,0), COM at (-1,2).
    scene.instruments.push_back(instrument(InstrumentKind::Generator, "gen1", {-3, 1}));
    addRoute(scene, {-1, 0}, {2, 0});
    addRoute(scene, {-1, 2}, {-1, 6});
    addRoute(scene, {-1, 6}, {8, 6});
    // scope1: CH1 at (13,-2) on R1's left end, CH2 at (13,0) on `out`.
    scene.instruments.push_back(instrument(InstrumentKind::Scope, "scope1", {16, -1}));
    addRoute(scene, {13, -2}, {0, -2});
    addRoute(scene, {0, -2}, {0, 0});
    addRoute(scene, {8, 0}, {13, 0});
    // dmm1 across R2: + at (11,10) up to the wire to CH2, COM at (13,10) to ground.
    scene.instruments.push_back(instrument(InstrumentKind::Multimeter, "dmm1", {12, 8}));
    addRoute(scene, {11, 10}, {11, 12});
    addRoute(scene, {11, 12}, {16, 12});
    addRoute(scene, {16, 12}, {16, 3});
    addRoute(scene, {16, 3}, {10, 3});
    addRoute(scene, {10, 3}, {10, 0});
    addRoute(scene, {13, 10}, {13, 14});
    addRoute(scene, {13, 14}, {8, 14});
    addRoute(scene, {8, 14}, {8, 6});

    const Conversion conversion = convert(scene);
    CHECK(conversion.diagnostics.empty());
    CHECK_EQ(conversion.instruments.size(), 3u);
    if (conversion.instruments.size() == 3) {
        CHECK(conversion.instruments[0].nets == (std::vector<std::string>{"N1", "0"}));
        CHECK(conversion.instruments[1].nets == (std::vector<std::string>{"N1", "out"}));
        CHECK(conversion.instruments[2].nets == (std::vector<std::string>{"out", "0"}));
    }
}
