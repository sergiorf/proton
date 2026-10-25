// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/schematic/scene.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace proton::schematic {

// The editing the schematic editor does (PLAN.md M4.2), on the Qt-free scene so it is tested here:
// what lies under a point, new parts' names and values, wire routes, junction dots, and moving,
// turning and deleting parts with their wires following.

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

// The topmost (last drawn) part whose symbol covers `p`, the wire `p` lies on, or the label at `p`.
std::optional<std::size_t> partAt(const Scene& scene, Point p);
std::optional<std::size_t> wireAt(const Scene& scene, Point p);
std::optional<std::size_t> labelAt(const Scene& scene, Point p);

// The designator a new part of `kind` gets: its letter and the next number after the highest in
// use for that letter ("R3" after R1 and R2), ignoring case. Ground parts have none.
std::string nextDesignator(const Scene& scene, ComponentKind kind);
// A new part's value: 1k, 100n, 1m (henry), 5 (volt), 1m (ampere); none for ground.
std::string defaultValue(ComponentKind kind);

// The wires from `from` to `to` as a schematic editor draws them: horizontal first, then vertical.
// None if the points are the same, one if they share a row or column.
std::vector<Wire> route(Point from, Point to);

// Where the editor draws a junction dot: every explicit junction, and every point where three or
// more connections meet (wire ends and pins at it, two for each wire passing through it). Sorted.
std::vector<Point> junctionDots(const Scene& scene);

// Moves part `index` to `position`. Wire ends that were on its pins move with them, so the part
// stays connected.
void movePart(Scene& scene, std::size_t index, Point position);
// Turns part `index` a quarter turn clockwise about its position, its wire ends following.
void rotatePart(Scene& scene, std::size_t index);

// Removes the parts, wires and labels with these indices (any order, duplicates ignored).
void remove(Scene& scene, const std::vector<std::size_t>& parts,
            const std::vector<std::size_t>& wires, const std::vector<std::size_t>& labels);

} // namespace proton::schematic
