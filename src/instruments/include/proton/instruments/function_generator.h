// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/instruments/spice_fragment.h"

#include <string_view>

namespace proton::instruments {

// A bench function generator: an ideal voltage source behind a series output resistor, between an
// output net and a reference net (the student's ground). It acts on the circuit only, through
// toSpice(); it measures nothing.
//
// Settings are sanitised, never trusted (ARCHITECTURE.md section 5):
//
// - non-finite values are replaced by the defaults below;
// - frequency is clamped to [kMinFrequency, kMaxFrequency];
// - amplitude is clamped to [0, kMaxVoltage];
// - offset is clamped so the output stays within +-kMaxVoltage: |offset| + amplitude for the AC
//   waveforms, |offset| alone for DC;
// - output impedance is clamped to [0, kMaxOutputImpedance]; 0 is an ideal source.
class FunctionGenerator
{
public:
    enum class Waveform {
        Sine,
        Square,   // 50 % duty cycle
        Triangle, // symmetric
        Dc,       // the offset alone
    };

    static constexpr double kMinFrequency = 1e-3;      // Hz
    static constexpr double kMaxFrequency = 10e6;      // Hz
    static constexpr double kMaxVoltage = 10.0;        // V, open circuit
    static constexpr double kMaxOutputImpedance = 1e6; // ohm
    static constexpr double kEdgeFraction = 1e-3;      // square and triangle corners, of a period
    // An output switched off is open; SPICE cannot solve a net with nothing on it, so the open
    // output keeps this leak to the reference: no load on any circuit a student builds.
    static constexpr double kOffResistance = 1e9; // ohm

    // Voltages are open-circuit values, before the output impedance: with 50 ohm, a 50 ohm load
    // sees half. The default source is ideal (0 ohm), as measurements are (ARCHITECTURE.md
    // section 6): a student's RC corner lands on 1 / (2 pi R C), not shifted by a hidden 50 ohm.
    struct Config
    {
        Waveform waveform = Waveform::Sine;
        double frequency = 1e3;       // Hz; ignored for DC
        double amplitude = 1.0;       // V peak (half of Vpp); ignored for DC
        double offset = 0.0;          // V; the level for DC
        double outputImpedance = 0.0; // ohm; 0 is an ideal source
        bool output = true;            // the output switch; off opens the output

        friend bool operator==(const Config&, const Config&) = default;
    };

    // The settings as the generator would apply them.
    static Config sanitise(const Config& config);

    FunctionGenerator() = default;
    explicit FunctionGenerator(const Config& config) { configure(config); }

    // Applies sanitised settings, like turning the knobs.
    void configure(const Config& config) { config_ = sanitise(config); }
    const Config& config() const { return config_; }

    // The generator as SPICE elements named after the instance name `name` ("gen1"), which must be
    // a SPICE token that no designator uses:
    //
    //     * function generator gen1
    //     Vgen1 gen1_int 0 SIN(0 1 1k)
    //     Rgen1 gen1_int out 50
    //
    // - Sine is `SIN(offset amplitude frequency)`, starting at the offset and rising.
    // - Square and triangle are `PULSE` sources with finite corners of kEdgeFraction of a period:
    //   square edges take that long, and the triangle dwells that long at each peak. Both start
    //   at their minimum at t = 0 and have the offset as their mean.
    // - DC is `DC offset`.
    // - With an output impedance of 0 there is no resistor and the source sits directly between
    //   `outputNet` and `referenceNet`.
    // - With the output off, whatever the waveform and impedance, the output is open: a 0 V source
    //   behind kOffResistance, headed `* function generator gen1, output off`.
    //
    // Numbers are written with formatSpiceNumber, rounded to 12 significant digits first so that
    // derived times read as written (`499u`, not `498.99999999999994u`). Deterministic.
    SpiceFragment toSpice(std::string_view name, std::string_view outputNet,
                          std::string_view referenceNet) const;

private:
    Config config_;
};

} // namespace proton::instruments
