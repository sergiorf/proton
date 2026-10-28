// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/instruments/dc_power_supply.h"
#include "proton/instruments/function_generator.h"
#include "proton/instruments/multimeter.h"
#include "proton/instruments/scope.h"
#include "proton/schematic/circuit.h"
#include "proton/schematic/validate.h"
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

// A terminal on a net the simulation does not have.
struct ProbeError
{
    std::string probe;   // the instrument and terminal: "scope1 CH2", "dmm1 COM"
    std::string net;     // the net it is connected to
    std::string message; // one sentence for the student

    friend bool operator==(const ProbeError&, const ProbeError&) = default;
};

// The instruments on the bench and how they are wired to the circuit (ARCHITECTURE.md section
// 5.1, ADR 0009), on one thread and without a clock: any number of function generators, DC power
// supplies, scopes and multimeters, each a named instance with a net per terminal.
//
// - A generator drives its output between `+` and `COM`; COM may be any net. It is connected only
//   when both terminals have a net, and then contributes its SPICE elements named after the
//   instance ("Vgen1", "Rgen1").
// - A supply drives each channel between its `+` and `-`, a channel being connected when both
//   have a net ("Vpsu1_1"). Its displays read `v(+) - v(-)` and the current out of `+`, the
//   negative of its source's branch current; a channel not connected reads its open output: the
//   set voltage while on, and no current.
// - A scope's CH1 and CH2 each measure their net against ground (single-ended).
// - A multimeter reads `v(+) - v(COM)`: it floats, as a handheld meter does.
//
// A terminal with no net, or on ground (node 0 or a net a Ground component sits on), is at 0 V. A
// terminal on a net the simulation does not have is a ProbeError, and its instrument is not fed
// until the terminal moves or the circuit changes: a clear error instead of a flat line. Net names
// are compared ignoring case, as SPICE does.
//
// A run goes: contributeTo() the circuit, check() the names, load the netlist into an engine,
// attach() its signal names, then feed() its output. restart() starts the instruments again for a
// new run from t = 0. Instruments are addressed by their index in the configuration.
class Bench
{
public:
    // The engine's longest step with no scope on the bench, s: nothing on the bench samples.
    static constexpr double kMaxStepWithoutScope = 1e-5;

    struct GeneratorSettings
    {
        std::string name = "gen1";
        instruments::FunctionGenerator::Config config;
        std::string plus; // the output's net; empty: not connected
        std::string com;  // the output's common; empty: not connected

        friend bool operator==(const GeneratorSettings&, const GeneratorSettings&) = default;
    };

    struct ScopeSettings
    {
        std::string name = "scope1";
        instruments::Scope::Config config;
        std::array<std::string, instruments::Scope::kChannels> channels; // CH1, CH2; empty: none

        friend bool operator==(const ScopeSettings&, const ScopeSettings&) = default;
    };

    struct MultimeterSettings
    {
        std::string name = "dmm1";
        instruments::Multimeter::Config config;
        std::string plus; // empty: not connected
        std::string com;  // empty: not connected

        friend bool operator==(const MultimeterSettings&, const MultimeterSettings&) = default;
    };

    struct SupplySettings
    {
        std::string name = "psu1";
        instruments::DcPowerSupply::Config config;
        // Each channel's + and -; a channel is connected only when both have a net.
        std::array<instruments::DcPowerSupply::Terminals, instruments::DcPowerSupply::kChannels>
            channels;

        friend bool operator==(const SupplySettings&, const SupplySettings&) = default;
    };

    // Plain data, so it can be compared, copied and saved without touching the models.
    struct Config
    {
        std::vector<GeneratorSettings> generators;
        std::vector<ScopeSettings> scopes;
        std::vector<MultimeterSettings> multimeters;
        std::vector<SupplySettings> supplies;

        friend bool operator==(const Config&, const Config&) = default;
    };

    Bench() = default;
    explicit Bench(const Config& config) { configure(config); }

    // Applies the settings, like turning knobs: an instrument whose settings change starts its
    // acquisition again (Scope::configure, Multimeter::configure), the others carry on. Moved
    // terminals are attached again to the signals of the last attach().
    void configure(const Config& config);
    const Config& config() const { return config_; }

    // The engine's longest step: maxStepFor() the fastest scope, or kMaxStepWithoutScope.
    double maxStep() const;

    // Adds the fragment of every connected generator and supply under its instance name, and
    // remembers which of the circuit's nets are ground.
    void contributeTo(schematic::Circuit& circuit);

    // Diagnostics for the instance names: empty, not unique (ignoring case), or the designator of
    // one of `circuit`'s components. Instance names and designators share one name space
    // (ADR 0009). Errors, so the circuit does not run until they are fixed.
    std::vector<schematic::Diagnostic> check(const schematic::Circuit& circuit) const;

    // Resolves every terminal's net to one of `signalNames`, the engine's signals ("v(out)"), and
    // keeps the names for configure(). Returns probeErrors().
    const std::vector<ProbeError>& attach(std::span<const std::string> signalNames);

    // The terminals attach() could not resolve, in order: scopes, multimeters, then supplies.
    const std::vector<ProbeError>& probeErrors() const { return errors_; }

    // Starts every instrument again for a new run from t = 0, keeping the settings.
    void restart();

    // Feeds points [begin, end) of `chunk` to every instrument whose terminals are attached.
    void feed(const sim::SimChunk& chunk, std::size_t begin, std::size_t end);

    // Starts scope `scope`'s acquisition again with the same settings: pressing Single once more.
    void rearmScope(std::size_t scope);
    // Takes a frame from scope `scope` at once without a trigger: its Force button (Scope::force).
    void forceScope(std::size_t scope);
    // True once scope `scope` in Single mode has taken its frame (Scope::stopped).
    bool scopeStopped(std::size_t scope) const;

    // The newest frame or reading not yet popped, as Scope::popFrame and Multimeter::popReading;
    // nothing for an index past the instruments.
    std::optional<instruments::ScopeFrame> popFrame(std::size_t scope);
    std::optional<instruments::DmmReading> popReading(std::size_t multimeter);
    std::optional<instruments::SupplyReading> popSupplyReading(std::size_t supply);

private:
    // Where a terminal's samples come from.
    struct Source
    {
        enum class Kind {
            Zero,    // no net, or ground
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
    std::vector<instruments::Scope> scopes_;
    std::vector<instruments::Multimeter> multimeters_;
    std::vector<instruments::DcPowerSupply> supplies_;

    std::vector<std::string> groundNets_; // lower case
    bool attached_ = false;               // attach() has been called
    std::vector<std::string> signalNames_;
    std::vector<std::array<Source, instruments::Scope::kChannels>> scopeSources_;
    std::vector<std::array<Source, 2>> multimeterSources_; // +, COM
    // Per supply channel: +, -, and the source's branch current. None of them is resolved for a
    // channel that is not connected.
    struct SupplyChannelSources
    {
        bool connected = false;
        Source plus;
        Source minus;
        Source current;
    };
    std::vector<std::array<SupplyChannelSources, instruments::DcPowerSupply::kChannels>>
        supplySources_;
    std::vector<ProbeError> errors_;
    std::vector<double> zeros_;
    std::vector<double> difference_; // v(+) - v(COM) for the multimeter being fed
    // A supply's channels being fed: their voltages and the currents they deliver.
    std::array<std::vector<double>, instruments::DcPowerSupply::kChannels> supplyVoltages_;
    std::array<std::vector<double>, instruments::DcPowerSupply::kChannels> supplyCurrents_;
};

} // namespace proton::bench
