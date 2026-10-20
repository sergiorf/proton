// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/schematic/circuit.h"

#include <string>
#include <string_view>
#include <vector>

namespace proton::schematic {

enum class Severity {
    Warning, // the circuit simulates, but probably not as the student meant
    Error,   // the simulator would reject the circuit or fail on it
};

enum class DiagnosticCode {
    NoGround,            // no Ground component and no element on node 0
    InvalidDesignator,   // empty, not a valid name, or not starting with the kind's letter
    DuplicateDesignator, // same element name twice, components and fragments, ignoring case
    InvalidTerminals,    // wrong number of nets, or an empty net name
    InvalidNetName,      // a net name SPICE would not read as one node name
    InvalidValue,        // empty or not a SPICE number
    ZeroValue,           // a resistor, capacitor or inductor of zero
    InvalidFragmentLine, // a fragment line validate() cannot read (see validate)
    SingleConnectionNet, // a net only one terminal connects to (warning)
    NoDcPathToGround,    // a net with no DC path to ground: SPICE's "singular matrix"
};

// Stable name of a code, "NoGround", for logs and tests.
std::string_view codeName(DiagnosticCode code);

struct Diagnostic
{
    Severity severity = Severity::Error;
    DiagnosticCode code = DiagnosticCode::NoGround;
    std::string element; // designator or fragment element it is about, if any
    std::string net;     // net it is about, if any, as first spelled in the circuit
    std::string message; // one sentence for the student

    friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

// Checks a circuit for the mistakes beginners make most, before the simulator turns them into
// cryptic errors. The result is empty for a clean circuit and is deterministic: checks run in the
// order of the codes above, and within a check in circuit order (components, then fragments).
//
// Designators and net names may only contain letters, digits and `_ - + .`, so each is one SPICE
// token. Names follow SPICE, which ignores case: `R1` and `r1` are the same element, and nets `Out`
// and `out` are the same net. Ground is node `0` together with every net a Ground component is on.
// Ground components are not SPICE elements, so their designators and values are not checked.
//
// Fragment lines are read as `<name> <node> <node> ...`. The element letter must be R, C, L, V or
// I; other lines are an InvalidFragmentLine, so an instrument that starts emitting a new element
// type fails its tests instead of silently escaping the connectivity checks. Blank lines and `*`
// comments are skipped.
//
// For the DC path, resistors, inductors and voltage sources conduct; capacitors are open and
// current sources ideal, so neither does. Without any ground, only NoGround is reported for
// connectivity, since every net would lack a path.
std::vector<Diagnostic> validate(const Circuit& circuit);

// True if any diagnostic is an Error: the circuit must not be simulated.
bool hasErrors(const std::vector<Diagnostic>& diagnostics);

} // namespace proton::schematic
