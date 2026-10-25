// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/bench/run_control.h"

namespace proton::bench {

// True when this build has NgspiceEngine (PROTON_HAVE_NGSPICE).
bool haveNgspice();

// The factory the app runs circuits with: an NgspiceEngine with the run's longest step and an
// endless stop time (ADR 0007). In a build without ngspice it makes no engine and says so. The
// MockEngine ignores the circuit, so it is never a fallback here; it stays in tests and
// proton-spike (ARCHITECTURE.md section 8).
RunControl::EngineFactory ngspiceEngines();

} // namespace proton::bench
