// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/schematic/scene.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace proton::schematic {

// The editing the schematic editor does (PLAN.md M4.2, M4.4, M4.12), on the Qt-free scene so it is
// tested here: what lies under a point, new parts' and instruments' names and values, wire routes,
// junction dots, moving, turning and deleting parts and instruments with their wires following,
// and wiring.

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

// ---- Wiring as a desktop circuit simulator does it (PLAN.md M4.12) ----------------------------
//
// Wires stay horizontal and vertical: moving, turning or mirroring a part routes its wires again
// instead of stretching them, a wire can be dragged sideways or by its end, and a wire can be
// drawn from any pin. The routes never change what is connected to what: a route may cross a wire
// (a crossing does not connect), but never bends on one, runs along one, or passes over a pin,
// terminal, wire end, label or junction, any of which would join it to that net.

// Whether a part's pin or an instrument's terminal is at `p`.
bool pinAt(const Scene& scene, Point p);

// The wire a press at `p` grabs by its end: the only wire that ends at `p`, when no other wire
// runs through `p` and no junction or label is there. A pin or terminal may be: that is how a
// wire's end is moved off one, onto another.
std::optional<std::size_t> wireEndAt(const Scene& scene, Point p);

// Whether `wire` runs through the inside of a part's or instrument's symbol, its edge, where the
// pins are, excluded: drawn over the symbol, it would look connected where it is not, or short the
// part where it passes over both its pins.
bool crossesSymbol(const Scene& scene, const Wire& wire);

// The wires the Wire tool draws from `from` to `to`: route()'s, which join what they pass over,
// unless one of them runs through a symbol; then routeAround()'s, round it.
std::vector<Wire> wireToolRoute(const Scene& scene, Point from, Point to);

// A wire from `from` to `to` through the scene as it is: horizontal and vertical segments, as
// short as it can be with as few bends as it can, around the symbols in its way when there is
// room (a route may go through a symbol when there is no other way), and touching nothing of the
// drawing except at its two ends (above). None if the points are the same; route()'s two
// segments if no such route exists near them.
std::vector<Wire> routeAround(const Scene& scene, Point from, Point to);

// Moves the parts, instruments, wires, labels and junctions of `selection` by `offset`, as a drag
// does. A selected wire moves whole. Every other wire chain that ends on something that moves (a
// pin, a terminal, the end of a selected wire, a selected junction or label) is routed again, with
// routeAround(), from where that end is now to where the chain first meets something else (a pin,
// a junction, a third wire or a label): its far end. A chain whose far end moves too moves with it.
// Whatever stayed where a moving pin was (another pin, a label, a wire passing by) is joined to
// it again by a route of its own, so no connection is lost. Returns where the selection's items
// are now: parts, instruments, labels and junctions keep their indices, wires may not.
Selection moveSelection(Scene& scene, const Selection& selection, Point offset);

// Turns each part and instrument of `selection` a quarter turn clockwise, or mirrors it left to
// right, about its own position (rotatePart(), mirrorPart()), its wires routed again as
// moveSelection() routes them. Selected wires, labels and junctions stay.
void rotateSelection(Scene& scene, const Selection& selection);
void mirrorSelection(Scene& scene, const Selection& selection);

// Drags wire `index` sideways by `offset`: a horizontal wire up or down by offset.y, a vertical
// one left or right by offset.x (a slanted one, from an older drawing, by both). At each of its
// ends, a single wire across it, with nothing else there, stretches to follow; anything else at
// the end (a pin, a junction, more wires) is joined to the moved end by a new straight wire, as is
// anything that lay along its middle.
void dragSegment(Scene& scene, std::size_t index, Point offset);

// Moves the end of wire `index` that is at `end` to `to`, as dragging a wire's end does: the chain
// from that end to its far end (moveSelection()) is routed again from `to`, ending on a wire's
// middle taps it (tapWire()). Whatever was at `end` stays and is no longer joined to the wire: a
// wire end dragged off a pin into empty sheet disconnects it. Nothing if `end` is not an end of
// the wire.
void dragWireEnd(Scene& scene, std::size_t index, Point end, Point to);

// Splits the wire whose middle `p` lies on into two at `p`, so a wire ending there meets ends.
// Returns whether a wire was split.
bool tapWire(Scene& scene, Point p);

// Tidies the wires after an edit: drops wires of no length and wires drawn twice, and merges two
// wires that continue each other in a straight line when nothing but the two of them is at the
// point where they meet. Connections are unchanged.
void tidyWires(Scene& scene);

// Whether `a` and `b` have the same parts and instruments, in the same order, connected to one
// another in the same groups: their pins and terminals fall into the same nets, whatever those
// nets are called and however their wires run.
bool sameNets(const Scene& a, const Scene& b);

} // namespace proton::schematic
