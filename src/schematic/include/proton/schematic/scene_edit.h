// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/schematic/scene.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace proton::schematic {

// The editing the schematic editor does (PLAN.md M4.2, M4.4), on the Qt-free scene so it is tested
// here: what lies under a point, new parts' and instruments' names and values, wire routes,
// junction dots, and moving, turning and deleting parts and instruments with their wires following.

// A rectangle on the grid, edges included.
struct Box
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    bool contains(Point p) const
    {
        return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
    }

    friend bool operator==(const Box&, const Box&) = default;
};

// The area a part's symbol covers, placed: two-terminal parts 4 x 2 between their pins, sources
// 2 x 4, ground 2 wide below its pin.
Box bounds(const PlacedPart& part);
// The area an instrument's symbol covers, placed, with its terminals on its edge: a generator
// 6 x 4 with them on its right, a scope 7 x 6 with them on its left, a multimeter 4 x 5 with them
// along its bottom.
Box bounds(const PlacedInstrument& instrument);

// The topmost (last drawn) part or instrument whose symbol covers `p`, the wire `p` lies on, or
// the label at `p`.
std::optional<std::size_t> partAt(const Scene& scene, Point p);
std::optional<std::size_t> instrumentAt(const Scene& scene, Point p);
std::optional<std::size_t> wireAt(const Scene& scene, Point p);
std::optional<std::size_t> labelAt(const Scene& scene, Point p);

// The designator a new part of `kind` gets: its letter and the next number after the highest in
// use for that letter ("R3" after R1 and R2), ignoring case. Ground parts have none.
std::string nextDesignator(const Scene& scene, ComponentKind kind);
// A new part's value: 1k, 100n, 1m (henry), 5 (volt), 1m (ampere); none for ground.
std::string defaultValue(ComponentKind kind);
// The name a new instrument of `kind` gets: "gen", "scope" or "dmm" and the next number after the
// highest in use for that prefix ("scope2" after scope1), ignoring case (ADR 0009).
std::string nextInstrumentName(const Scene& scene, InstrumentKind kind);

// The wires from `from` to `to` as a schematic editor draws them: horizontal first, then vertical.
// None if the points are the same, one if they share a row or column.
std::vector<Wire> route(Point from, Point to);

// Where the editor draws a junction dot: every explicit junction, and every point where three or
// more connections meet (wire ends, pins and instrument terminals at it, two for each wire passing
// through it). Sorted.
std::vector<Point> junctionDots(const Scene& scene);

// Moves part `index` to `position`. Wire ends that were on its pins move with them, so the part
// stays connected.
void movePart(Scene& scene, std::size_t index, Point position);
// Turns part `index` a quarter turn clockwise about its position, its wire ends following.
void rotatePart(Scene& scene, std::size_t index);
// The same for instrument `index` and the wire ends on its terminals.
void moveInstrument(Scene& scene, std::size_t index, Point position);
void rotateInstrument(Scene& scene, std::size_t index);

// Mirrors part `index` left to right on screen about its position, its wire ends following: the
// same placement turned the other way and mirrored in its own frame (PLAN.md M4.7).
void mirrorPart(Scene& scene, std::size_t index);
void mirrorInstrument(Scene& scene, std::size_t index);

// The explicit junction at `p`.
std::optional<std::size_t> junctionAt(const Scene& scene, Point p);

// Removes the parts, wires, labels, instruments and junctions with these indices (any order,
// duplicates ignored).
void remove(Scene& scene, const std::vector<std::size_t>& parts,
            const std::vector<std::size_t>& wires, const std::vector<std::size_t>& labels,
            const std::vector<std::size_t>& instruments = {},
            const std::vector<std::size_t>& junctions = {});

// The area everything drawn covers: parts' and instruments' symbols, wires, labels and junctions.
// None for an empty scene.
std::optional<Box> bounds(const Scene& scene);

// Indices into a scene's lists: what the editor has selected, or what paste() added.
struct Selection
{
    std::vector<std::size_t> parts;
    std::vector<std::size_t> wires;
    std::vector<std::size_t> labels;
    std::vector<std::size_t> instruments;
    std::vector<std::size_t> junctions;

    bool empty() const
    {
        return parts.empty() && wires.empty() && labels.empty() && instruments.empty() &&
               junctions.empty();
    }

    friend bool operator==(const Selection&, const Selection&) = default;
};

// What a selection box drawn on the sheet selects (PLAN.md M4.10): the parts and instruments whose
// symbols lie wholly inside `box`, the labels and junctions at a point inside it, and the wires
// with both ends inside it, so a wire only half inside is not selected. In scene order.
Selection within(const Scene& scene, const Box& box);

// The selected items as a scene of their own, in scene order: what Copy puts on the clipboard.
Scene extract(const Scene& scene, const Selection& selection);

// Adds `clip` to `scene`, moved by `offset`, and returns where its items went. Pasted parts get the
// next free designators of their kinds and instruments the next free names, so nothing pasted
// clashes with what is there (R3 and R4 pasted beside R1 and R2); labels keep their names, so a
// pasted label joins the net of that name.
Selection paste(Scene& scene, const Scene& clip, Point offset);

// Whether an instrument other than `except` is called `name`, ignoring case.
bool instrumentNameTaken(const Scene& scene, const std::string& name,
                         std::optional<std::size_t> except = std::nullopt);

} // namespace proton::schematic
