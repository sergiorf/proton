// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/sim/sim_chunk.h"

#include <string>
#include <string_view>
#include <vector>

namespace proton::sim {

// A transient simulator, pulled one chunk at a time (ARCHITECTURE.md section 8). Everything
// above src/sim talks to this interface, so engines are interchangeable.
//
// An engine is used from one thread at a time (the SimWorker's). It reports errors as values,
// never by throwing: load() returns false, a failing advance() stops the run, and error() says
// why.
class ISimEngine
{
public:
    virtual ~ISimEngine() = default;

    ISimEngine(const ISimEngine&) = delete;
    ISimEngine& operator=(const ISimEngine&) = delete;

    // Loads a netlist as schematic::buildNetlist() writes it (title line and elements; the
    // engine adds the analysis) and prepares a transient run from t = 0. Replaces any circuit
    // loaded before. On failure nothing is loaded, it returns false and error() says why.
    virtual bool load(std::string_view netlist) = 0;

    // Why the last load() failed, or why the run stopped in advance(). Empty while all is well:
    // a successful load() clears it, and so does reset() of a loaded circuit.
    virtual const std::string& error() const = 0;

    // Names of the signals in every chunk, in order, as SPICE names them ("v(out)"). Empty when
    // nothing is loaded.
    virtual const std::vector<std::string>& signalNames() const = 0;

    // Simulates the next `duration` seconds and returns the points in them: the first call after
    // load() or reset() includes t = 0, later calls continue after the previous one. The chunk
    // has one signal per name, and is empty when nothing is loaded or `duration` is not a
    // positive number. If the run fails, the chunk holds the points before the failure and
    // error() says why; later calls give empty chunks until load() or reset().
    virtual SimChunk advance(double duration) = 0;

    // Goes back to t = 0 with the loaded circuit.
    virtual void reset() = 0;

protected:
    ISimEngine() = default;
};

} // namespace proton::sim
