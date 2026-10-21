// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace proton::sim {

// A block of simulation output (ARCHITECTURE.md section 3). Time points are strictly increasing,
// continue from the previous chunk, and are unevenly spaced: the engine picks its own steps.
// There is one signal per name in ISimEngine::signalNames(), in the same order.
//
// Move-only: chunks travel between threads by move through SpscRing, never by copy.
struct SimChunk
{
    std::vector<double> time;                 // simulation time, s
    std::vector<std::vector<double>> signals; // signals[s][i]: signal s at time[i]

    SimChunk() = default;
    SimChunk(const SimChunk&) = delete;
    SimChunk& operator=(const SimChunk&) = delete;
    SimChunk(SimChunk&&) noexcept = default;
    SimChunk& operator=(SimChunk&&) noexcept = default;
    ~SimChunk() = default;

    bool empty() const { return time.empty(); }

    friend bool operator==(const SimChunk&, const SimChunk&) = default;
};

// The index of the signal called `name` in `names`, if there is one. Names are compared ignoring
// ASCII case, as SPICE does: "V(Out)" finds "v(out)". The first match wins.
std::optional<std::size_t> indexOfSignal(std::span<const std::string> names, std::string_view name);

} // namespace proton::sim
