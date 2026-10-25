// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/schematic/circuit.h"
#include "proton/schematic/validate.h"

#include <string>
#include <string_view>
#include <vector>

namespace proton::schematic {

// The schematic as the student draws it (PLAN.md M4.1): placed parts, placed instruments, wires,
// junctions and net labels, on an integer grid. Plain data without Qt: the editor (M4.2) draws it,
// and convert() turns it into the Circuit that is simulated and the nets each instrument terminal
// is on (ARCHITECTURE.md section 7, ADR 0009). Instruments are parts of the scene but not of the
// Circuit: their settings and models live in the bench, so `schematic` does not depend on
// `instruments`.

// A point on the grid, in grid units. x grows to the right and y downwards, as on screen.
struct Point
{
    int x = 0;
    int y = 0;

    friend bool operator==(const Point&, const Point&) = default;
    friend auto operator<=>(const Point&, const Point&) = default;
};

// Quarter turns clockwise on screen.
enum class Rotation {
    R0,
    R90,
    R180,
    R270,
};

// Where a part or instrument sits and how it is turned. Its pins are its pin offsets, mirrored
// left to right first when `mirrored`, then turned by `rotation`, then moved to `position`.
struct Placement
{
    Point position;
    Rotation rotation = Rotation::R0;
    bool mirrored = false;

    friend bool operator==(const Placement&, const Placement&) = default;
};

// A component on the sheet; it becomes a Component of the Circuit.
struct PlacedPart
{
    ComponentKind kind = ComponentKind::Resistor;
    std::string designator; // "R1"; ground symbols have none of their own
    std::string value;      // "1k"
    Placement placement;

    friend bool operator==(const PlacedPart&, const PlacedPart&) = default;
};

enum class InstrumentKind {
    Generator,  // terminals + and COM
    Scope,      // terminals CH1 and CH2
    Multimeter, // terminals + and COM
};

// An instrument on the sheet (ADR 0009): a symbol with terminals, wired like a part.
struct PlacedInstrument
{
    InstrumentKind kind = InstrumentKind::Scope;
    std::string name; // "scope1"
    Placement placement;

    friend bool operator==(const PlacedInstrument&, const PlacedInstrument&) = default;
};

// A straight wire between two grid points.
struct Wire
{
    Point from;
    Point to;

    friend bool operator==(const Wire&, const Wire&) = default;
};

// A net name placed on the sheet. It names the net of whatever it touches, and every label with the
// same name, ignoring case, is the same net.
struct NetLabel
{
    std::string name;
    Point position;

    friend bool operator==(const NetLabel&, const NetLabel&) = default;
};

struct Scene
{
    std::vector<PlacedPart> parts;
    std::vector<PlacedInstrument> instruments;
    std::vector<Wire> wires;
    std::vector<Point> junctions; // dots that connect wires crossing there
    std::vector<NetLabel> labels;

    friend bool operator==(const Scene&, const Scene&) = default;
};

// A component kind's pin offsets in its own frame, in the order of Component::nets: two-terminal
// parts lie horizontally with pins at (-2, 0) and (2, 0); sources stand upright with + at (0, -2)
// and - at (0, 2); ground has its one pin at (0, 0).
std::vector<Point> pinOffsets(ComponentKind kind);

// An instrument kind's terminal names and offsets, in the same order: a generator's + and COM at
// (2, -1) and (2, 1) on its right; a scope's CH1 and CH2 at (-3, -1) and (-3, 1) on its left; a
// multimeter's + and COM at (-1, 2) and (1, 2) along its bottom.
std::vector<std::string_view> terminalNames(InstrumentKind kind);
std::vector<Point> terminalOffsets(InstrumentKind kind);

// Where an offset ends up for a placement.
Point place(Point offset, const Placement& placement);

// The nets of one instrument's terminals, in the order of terminalNames(). An empty net is a
// terminal wired to nothing in the circuit.
struct InstrumentConnections
{
    InstrumentKind kind = InstrumentKind::Scope;
    std::string name;
    std::vector<std::string> nets;

    friend bool operator==(const InstrumentConnections&, const InstrumentConnections&) = default;
};

struct Conversion
{
    Circuit circuit;
    std::vector<InstrumentConnections> instruments; // in scene order
    std::vector<Diagnostic> diagnostics;            // from the conversion; validate() adds its own
};

// The circuit a scene draws, and the nets of its instruments.
//
// Connections, as in common schematic editors: a wire connects its two ends; a wire end, pin, label
// or junction that lies on a wire, at its end or along it, connects to it; two wires that only
// cross do not connect unless a junction sits on the crossing; pins, wire ends, labels and
// junctions at the same point connect.
//
// Net names: a net with a ground pin is "0"; otherwise a labelled net takes its label, as first
// spelled in scene order; otherwise nets are N1, N2, ... in the order of their smallest point (by
// x, then y). Names therefore depend only on the drawing, never on the order it was drawn in.
//
// Diagnostics: ConflictingLabels (error) for a net with two different labels, or a label on a
// ground net other than "0"; UnconnectedTerminal (warning) for an instrument terminal whose net has
// no component pin on it, whose net is then empty. Component pins that touch nothing get a net of
// their own, which validate() reports as a single connection.
Conversion convert(const Scene& scene);

} // namespace proton::schematic
