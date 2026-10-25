// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/bench/bench.h"
#include "proton/schematic/circuit.h"
#include "proton/schematic/scene.h"

#include <string>
#include <vector>

namespace proton::bench {

// A built-in circuit with the bench wired to it, for the app's example menu: drawn as a scene for
// the editor, the circuit converted from it. Until the instruments are on the sheet (M4.4), the
// bench finds the circuit through the net labels `in` and `out`.
struct Example
{
    std::string name;           // for the menu: "RC low-pass"
    schematic::Scene scene;     // the drawing, without the instruments
    schematic::Circuit circuit; // schematic::convert(scene).circuit
    Bench::Config bench;        // generator, probes and settings for it
};

// The examples, in menu order. Each validates without errors. Every one has the instruments
// gen1, scope1 and dmm1 (ADR 0009): scope1's CH1 on net `in` and CH2 on net `out`, and dmm1
// between `out` and ground:
//
// - RC low-pass: gen1 (1 kHz sine, 1 V, 50 ohm) between `in` and ground, R1 1k from `in` to
//   `out`, C1 100n from `out` to ground. The multimeter reads AC V.
// - Voltage divider: V1, a 5 V battery, on `in`, R1 10k from `in` to `out`, R2 10k from `out` to
//   ground; gen1 is on the bench but not connected. The multimeter reads DC V: 2.5 V.
// - RC high-pass: gen1 on `in`, C1 100n from `in` to `out`, R1 1k from `out` to ground.
//   The multimeter reads AC V.
std::vector<Example> examples();

} // namespace proton::bench
