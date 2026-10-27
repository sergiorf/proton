// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/schematic/scene.h"
#include "proton/schematic/validate.h"

#include <cstddef>
#include <vector>

namespace proton::schematic {

// Where a diagnostic is on the sheet (PLAN.md M4.9), for the editor to mark: indices into the
// scene's parts, instruments, wires and labels, and the pins it is about.
struct DiagnosticPlace
{
    std::vector<std::size_t> parts;
    std::vector<std::size_t> instruments;
    std::vector<std::size_t> wires;
    std::vector<std::size_t> labels;
    std::vector<Point> pins; // part pins and instrument terminals, placed

    // Nothing on the sheet: a diagnostic about the whole drawing, such as NoGround.
    bool empty() const
    {
        return parts.empty() && instruments.empty() && wires.empty() && labels.empty() &&
               pins.empty();
    }

    friend bool operator==(const DiagnosticPlace&, const DiagnosticPlace&) = default;
};

// The place of `diagnostic` in `scene`, which `conversion` is convert(scene) of. Names compare
// ignoring case, as SPICE does (validate.h).
//
// - A diagnostic about a net is on that net: its wires and the pins and terminals on it, and the
//   labels that name it or carry the diagnostic's net name (ConflictingLabels names the label,
//   not the net it is on).
// - Otherwise one about an element is on it: the parts with that designator, ground symbols
//   excepted, and the instrument of that name or whose fragment element it is (`Vgen1` is gen1's).
//   An InvalidDesignator with no name is on the parts that have none.
// - UnconnectedTerminal is on its instrument and on the terminals of it wired to nothing.
// - The others are on nothing: NoGround, and those a drawing cannot give (InvalidFragmentLine).
DiagnosticPlace locate(const Scene& scene, const Conversion& conversion,
                       const Diagnostic& diagnostic);

// True if grid point `p` is on `place`: on one of its pins, wires or labels, or within the symbol
// of one of its parts or instruments.
bool contains(const Scene& scene, const DiagnosticPlace& place, Point p);

} // namespace proton::schematic
