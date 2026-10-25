// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/bench/bench.h"
#include "proton/schematic/circuit.h"

#include <string>
#include <vector>

namespace proton::bench {

// A built-in circuit with the bench wired to it, for the app's example menu until the schematic
// editor comes (M4).
struct Example
{
    std::string name;            // for the menu: "RC low-pass"
    schematic::Circuit circuit;  // without the instruments
    Bench::Config bench;         // generator, probes and settings for it
};

// The examples, in menu order. Each validates without errors. Every one has the scope's CH1 on
// net `in` and CH2 on net `out`, and the multimeter on `out`:
//
// - RC low-pass: the generator (1 kHz sine, 1 V, 50 ohm) on `in`, R1 1k from `in` to `out`, C1
//   100n from `out` to ground. The multimeter reads AC V.
// - Voltage divider: V1, a 5 V battery, on `in`, R1 10k from `in` to `out`, R2 10k from `out` to
//   ground; the generator is not connected. The multimeter reads DC V: 2.5 V.
// - RC high-pass: the generator on `in`, C1 100n from `in` to `out`, R1 1k from `out` to ground.
//   The multimeter reads AC V.
std::vector<Example> examples();

} // namespace proton::bench
