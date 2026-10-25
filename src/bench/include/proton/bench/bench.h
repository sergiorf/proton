// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/instruments/function_generator.h"
#include "proton/instruments/multimeter.h"
#include "proton/instruments/scope.h"
#include "proton/schematic/circuit.h"
#include "proton/sim/sim_chunk.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace proton::bench {

// The engine's longest step for a scope set up as `scope`: one sample interval of its timebase
// (decision D6, ADR 0007), so the engine gives at least one point per sample and the scope's
// linear interpolation between points is faithful.
double maxStepFor(const instruments::Scope::Config& scope);

// A probe whose net the simulation does not have.
struct ProbeError
{
    std::string probe;   // "CH1", "CH2", "DMM1", ...
    std::string net;     // the net it is connected to
    std::string message; // one sentence for the student

    friend bool operator==(const ProbeError&, const ProbeError&) = default;
};

// The instruments on the bench and how they are wired to the circuit (ARCHITECTURE.md section
// 5.1), on one thread and without a clock: a function generator, a two-channel scope and any
// number of multimeters. M3.1 generalises it to any number of each.
//
// Every probe measures its net against ground. A probe with no net, or on ground (node 0 or a net
// a Ground component sits on), reads 0 V. A probe on a net the simulation does not have is a
// ProbeError, and its instrument is not fed until the probe moves or the circuit changes: a clear
// error instead of a flat line. Net names are compared ignoring case, as SPICE does.
//
// A run goes: contributeTo() the circuit, load the netlist into an engine, attach() its signal
// names, then feed() its output. restart() starts the instruments again for a new run from t = 0.
class Bench
{
public:
    // The generator's instance name, which its SPICE elements are named after ("Vgen1").
    static constexpr const char* kGeneratorName = "gen1";

    struct MultimeterSettings
    {
        instruments::Multimeter::Config config;
        std::string net; // empty: no probe

        friend bool operator==(const MultimeterSettings&, const MultimeterSettings&) = default;
    };

    // Plain data, so it can be compared, copied and saved without touching the models.
    struct Config
    {
        instruments::FunctionGenerator::Config generator;
        std::string generatorNet; // the output's net, against ground; empty: not connected
        instruments::Scope::Config scope;
        std::array<std::string, instruments::Scope::kChannels> scopeNets; // empty: no probe
        std::vector<MultimeterSettings> multimeters{1};

        friend bool operator==(const Config&, const Config&) = default;
    };

    Bench() = default;
    explicit Bench(const Config& config) { configure(config); }

    // Applies the settings, like turning knobs: an instrument whose settings change starts its
    // acquisition again (Scope::configure, Multimeter::configure), the others carry on. Moved
    // probes are attached again to the signals of the last attach().
    void configure(const Config& config);
    const Config& config() const { return config_; }

    // The engine's longest step: maxStepFor() the scope.
    double maxStep() const { return maxStepFor(config_.scope); }

    // Adds the generator's fragment under kGeneratorName, when it is connected, and remembers
    // which of the circuit's nets are ground.
    void contributeTo(schematic::Circuit& circuit);

    // Resolves every probe's net to one of `signalNames`, the engine's signals ("v(out)"), and
    // keeps the names for configure(). Returns probeErrors().
    const std::vector<ProbeError>& attach(std::span<const std::string> signalNames);

    // The probes attach() could not resolve, in order: CH1, CH2, then each multimeter.
    const std::vector<ProbeError>& probeErrors() const { return errors_; }

    // Starts every instrument again for a new run from t = 0, keeping the settings.
    void restart();

    // Feeds points [begin, end) of `chunk` to every instrument whose probes are attached.
    void feed(const sim::SimChunk& chunk, std::size_t begin, std::size_t end);

    // The newest frame or reading not yet popped, as Scope::popFrame and Multimeter::popReading.
    std::optional<instruments::ScopeFrame> popFrame() { return scope_.popFrame(); }
    std::optional<instruments::DmmReading> popReading(std::size_t multimeter);

private:
    // Where a probe's samples come from.
    struct Source
    {
        enum class Kind {
            Zero,    // no probe, or ground
            Signal,  // signals[index] of every chunk
            Missing, // a net the simulation does not have
        };
        Kind kind = Kind::Zero;
        std::size_t index = 0;
    };

    Source resolve(const std::string& net) const;
    void resolveAll();
    std::span<const double> samples(const Source& source, const sim::SimChunk& chunk,
                                    std::size_t begin, std::size_t count);

    Config config_;
    instruments::Scope scope_{config_.scope};
    std::vector<instruments::Multimeter> multimeters_{1};

    std::vector<std::string> groundNets_; // lower case
    bool attached_ = false; // attach() has been called
    std::vector<std::string> signalNames_;
    std::array<Source, instruments::Scope::kChannels> scopeSources_;
    std::vector<Source> multimeterSources_{1};
    std::vector<ProbeError> errors_;
    std::vector<double> zeros_;
};

} // namespace proton::bench
