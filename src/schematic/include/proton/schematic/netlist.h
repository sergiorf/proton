// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/schematic/circuit.h"

#include <string>
#include <string_view>

namespace proton::schematic {

// First line of every netlist: SPICE reads the first line as the title.
inline constexpr std::string_view kNetlistTitle = "* Proton netlist";

// The SPICE netlist of a circuit, for a circuit that validate() passes without errors. A pure
// function: the same circuit gives byte-identical text, whatever the order its components and
// fragments were added in.
//
//     * Proton netlist
//     C1 out 0 100n
//     R1 in out 1k
//     V1 in 0 DC 5
//     * gen1
//     Vgen1 gen1_int 0 SIN(0 1 1k)
//     Rgen1 gen1_int in 50
//
// - The title line, then one line per component sorted by designator in natural order, ignoring
//   case (R2 before R10), then each fragment sorted by name, headed by a `* <name>` comment.
// - Ground components write no line; every net they sit on is written as node `0`, in components
//   and in fragment lines alike, ignoring case. Other net names are written as they are.
// - Values are normalised with formatSpiceNumber ("10uF" -> "10u", "1MEG" -> "1meg"), so ngspice
//   never reads the student's spelling. Sources are written as `DC <value>`.
// - Fragment element lines keep everything after their two nodes as written; comments are kept,
//   blank lines dropped, and lines that are not R, C, L, V or I elements copied unchanged.
// - Lines end in "\n", including the last. There is no analysis line and no `.end`: the engine
//   appends both.
//
// For a circuit with errors the text is still deterministic, but not meaningful SPICE.
std::string buildNetlist(const Circuit& circuit);

} // namespace proton::schematic
