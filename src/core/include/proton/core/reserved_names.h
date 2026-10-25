// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

namespace proton::core {

// Names the simulator adds to every netlist, so no circuit may use them: the source and node
// NgspiceEngine halts ngspice with (ADR 0005). Here, below both, so the engine that adds them and
// validate(), which rejects them, use the same spelling. SPICE ignores case, and so do the checks.
inline constexpr const char* kReservedSource = "Vproton_halt";
inline constexpr const char* kReservedNode = "proton_halt";

} // namespace proton::core
