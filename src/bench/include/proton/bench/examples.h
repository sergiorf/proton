// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/bench/bench.h"
#include "proton/schematic/circuit.h"
#include "proton/schematic/scene.h"

#include <string>
#include <vector>

namespace proton::bench {

// A built-in circuit with its instruments, for the app's example menu: drawn as a scene for the
// editor, instruments included (ADR 0009), with the circuit and the bench that drawing gives.
struct Example
{
    std::string name;           // for the menu: "RC low-pass"
    schematic::Scene scene;     // the drawing, instruments wired on it
    schematic::Circuit circuit; // schematic::convert(scene).circuit
    Bench::Config bench;        // benchFor() the drawing's instruments, with their settings
};

// The examples, in menu order. Each validates without errors. Every one has scope1 with CH1 on
// net `in` and CH2 on net `out`, and dmm1 between `out` and ground, wired on the sheet:
//
// - RC low-pass: gen1 (1 kHz sine, 1 V, ideal source) between `in` and ground, R1 1k from `in`
//   to `out`, C1 100n from `out` to ground: its corner is 1 / (2 pi R C) = 1.592 kHz. The
//   multimeter reads AC V.
// - Voltage divider: V1, a 5 V battery, on `in`, R1 10k from `in` to `out`, R2 10k from `out` to
//   ground; no generator. The multimeter reads DC V: 2.5 V.
// - RC high-pass: gen1 on `in`, C1 100n from `in` to `out`, R1 1k from `out` to ground.
//   The multimeter reads AC V.
std::vector<Example> examples();

} // namespace proton::bench
