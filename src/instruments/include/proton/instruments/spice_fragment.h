// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>
#include <vector>

namespace proton::instruments {

// SPICE lines that an instrument acting on the circuit (a generator, a power supply) contributes
// to the netlist. The bench copies them into a schematic `Fragment` under the instrument's
// instance name; `instruments` does not depend on `schematic`.
struct SpiceFragment
{
    std::vector<std::string> lines;

    friend bool operator==(const SpiceFragment&, const SpiceFragment&) = default;
};

} // namespace proton::instruments
