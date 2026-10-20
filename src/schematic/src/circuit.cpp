// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/circuit.h"

namespace proton::schematic {

int terminalCount(ComponentKind kind)
{
    switch (kind) {
    case ComponentKind::Resistor:
    case ComponentKind::Capacitor:
    case ComponentKind::Inductor:
    case ComponentKind::VoltageSource:
    case ComponentKind::CurrentSource:
        return 2;
    case ComponentKind::Ground:
        return 1;
    }
    return 0;
}

} // namespace proton::schematic
