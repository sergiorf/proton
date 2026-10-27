// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/bench/bench.h"
#include "proton/schematic/scene.h"

#include <vector>

namespace proton::bench {

// The bench for the instruments a schematic draws (PLAN.md M4.4, ADR 0009): the sheet says which
// instruments there are and where their terminals are wired, and the bench keeps how each is set.

// One instance per instrument on the sheet, in sheet order within each kind, on the nets of its
// terminals (`instruments` from schematic::convert(); an empty net is a terminal wired to
// nothing). Each takes the settings of the instance of the same kind and name in `settings`,
// ignoring case, or the instrument's defaults when there is none.
Bench::Config benchFor(const std::vector<schematic::InstrumentConnections>& instruments,
                       const Bench::Config& settings);

// Records every instance of `current` in `settings`, replacing the one of the same kind and name.
// Instances no longer on the sheet stay, so an instrument deleted and brought back by undo has its
// settings again.
void remember(Bench::Config& settings, const Bench::Config& current);

// An instrument renamed on the sheet (PLAN.md M4.7) keeps its settings: records the instance of
// `kind` called `from` in `settings` under `to` as well, replacing any instance called `to`. The
// old name keeps them too, for undo. Does nothing when there is no instance called `from`.
void carryOver(Bench::Config& settings, schematic::InstrumentKind kind, const std::string& from,
               const std::string& to);

// Every diagnostic of a drawing, for the editor to show before anything runs (PLAN.md M4.9):
// the conversion's, then those RunControl finds when it starts the drawing on the bench `config`
// (validate() on the circuit with the instruments' fragments, then Bench::check). `conversion`
// is schematic::convert() of the drawing and `config` benchFor() its instruments.
std::vector<schematic::Diagnostic> diagnose(const schematic::Conversion& conversion,
                                            const Bench::Config& config);

} // namespace proton::bench
