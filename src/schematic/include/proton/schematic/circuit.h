// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace proton::schematic {

// The SPICE net name of ground. A net that a Ground component sits on is this node.
inline constexpr std::string_view kGroundNet = "0";

enum class ComponentKind {
    Resistor,
    Capacitor,
    Inductor,
    VoltageSource, // DC
    CurrentSource, // DC
    Ground,
};

// Number of nets a component of this kind connects: 1 for Ground, 2 for the others.
int terminalCount(ComponentKind kind);

// A component of the schematic, without any UI data (positions, rotations and symbols belong to
// the editor's scene model). Nets are names; two terminals on the same name are connected.
//
// Two-terminal components list nets as {positive, negative}: a voltage source raises `nets[0]`
// above `nets[1]`, and a current source drives its current from `nets[0]` through itself to
// `nets[1]`, as in SPICE. `value` is the text the user wrote ("4.7k", "10uF"), read with
// parseSpiceNumber; Ground has none.
struct Component
{
    ComponentKind kind = ComponentKind::Resistor;
    std::string designator; // "R1", "C2", "V1"
    std::vector<std::string> nets;
    std::string value;

    friend bool operator==(const Component&, const Component&) = default;
};

// SPICE lines contributed by an instrument (a generator's source and output resistor), copied
// into the netlist as they are. `name` is the unique instance name the bench gave the instrument
// ("gen1"); the element names inside the lines must not clash with any designator.
struct Fragment
{
    std::string name;
    std::vector<std::string> lines;

    friend bool operator==(const Fragment&, const Fragment&) = default;
};

// The circuit to simulate: plain data, in the order the components and fragments were added,
// which is the order the netlist keeps.
struct Circuit
{
    std::vector<Component> components;
    std::vector<Fragment> fragments;

    friend bool operator==(const Circuit&, const Circuit&) = default;
};

} // namespace proton::schematic
