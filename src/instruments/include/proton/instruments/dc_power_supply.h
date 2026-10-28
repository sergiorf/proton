// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/instruments/spice_fragment.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace proton::instruments {

// One channel of a reading of the supply's displays.
struct SupplyChannelReading
{
    double voltage = 0.0;    // mean output voltage over the gate, v(+) - v(-), V
    double current = 0.0;    // mean current out of +, through the load and back into -, A
    bool overLimit = false;  // |current| exceeded the limit at some point of the gate
    std::string voltageText; // what the voltage display shows: "5.000", "-12.000", "OL"
    std::string currentText; // what the current display shows: "0.0050", "OL"

    friend bool operator==(const SupplyChannelReading&, const SupplyChannelReading&) = default;
};

// One reading of both channels, taken at the end of a gate.
struct SupplyReading
{
    double time = 0.0; // simulation time at the end of the gate, s
    std::array<SupplyChannelReading, 2> channels;
};

// A two-channel bench DC power supply (PLAN.md M5.1): each channel an ideal voltage source between
// its `+` and `-` terminals, with an output switch and a current limit. It acts on the circuit
// through toSpice() and measures what it delivers through feed(), like a supply's own voltage and
// current displays.
//
// The current limit is a plain value, not CC/CV regulation: the output stays at its set voltage
// whatever it is asked for, and the reading flags a channel whose current went beyond the limit.
// Real constant-current behaviour comes after the MVP.
//
// The displays integrate over gates of kGateTime seconds of simulation time, as the multimeter
// does: each reading is the mean voltage and current over its gate, the signals taken as linear
// between the points fed, so the means are exact for any time grid. The limit is checked on every
// point, so a peak above it within a gate flags the reading even when the mean stays below.
//
// Settings are sanitised (ARCHITECTURE.md section 5): non-finite values become the defaults, the
// voltage is clamped to [0, kMaxVoltage] and the limit to [kMinCurrentLimit, kMaxCurrentLimit].
class DcPowerSupply
{
public:
    static constexpr std::size_t kChannels = 2;
    static constexpr double kMaxVoltage = 30.0;      // V
    static constexpr double kMinCurrentLimit = 1e-3; // A
    static constexpr double kMaxCurrentLimit = 3.0;  // A
    static constexpr double kGateTime = 0.1;         // s of simulation time per reading
    // An output switched off is open; as for the generator, SPICE needs a path, so the open output
    // keeps this leak between its terminals: no load on any circuit a student builds.
    static constexpr double kOffResistance = 1e9; // ohm

    struct ChannelConfig
    {
        double voltage = 5.0;      // V
        double currentLimit = 1.0; // A
        bool output = true;        // the output switch; off opens the output

        friend bool operator==(const ChannelConfig&, const ChannelConfig&) = default;
    };

    struct Config
    {
        std::array<ChannelConfig, kChannels> channels;

        friend bool operator==(const Config&, const Config&) = default;
    };

    // A channel's terminals: the nets of `+` and `-`. A channel with either empty is not
    // connected and contributes nothing.
    struct Terminals
    {
        std::string plus;
        std::string minus;

        friend bool operator==(const Terminals&, const Terminals&) = default;
    };

    // The settings as the supply would apply them.
    static Config sanitise(const Config& config);

    // The SPICE name of the source of channel `channel` (0 or 1) of instance `name`: "Vpsu1_1".
    // Its branch current, "i(vpsu1_1)", flows from + through the source to -, so the current the
    // channel delivers is its negative.
    static std::string sourceName(std::string_view name, std::size_t channel);

    // How the displays write a value: volts to the millivolt ("5.000"), amps to the tenth of a
    // milliamp ("0.0050"); a value too large for the display, or not finite, is "OL".
    static std::string formatVoltage(double volts);
    static std::string formatCurrent(double amps);

    DcPowerSupply() = default;
    explicit DcPowerSupply(const Config& config) { configure(config); }

    // Applies sanitised settings and restarts the displays: the gate in progress and any unread
    // reading are dropped.
    void configure(const Config& config);
    const Config& config() const { return config_; }

    // The supply as SPICE elements named after the instance name `name` ("psu1"), which must be a
    // SPICE token that no designator uses, one source per connected channel:
    //
    //     * DC power supply psu1
    //     Vpsu1_1 vcc 0 DC 5
    //
    // A channel whose output is off is open: a 0 V source behind kOffResistance through an
    // internal node ("psu1_2_int"), so its branch current is still there to read. No connected
    // channel, no lines. Deterministic.
    SpiceFragment toSpice(std::string_view name,
                          const std::array<Terminals, kChannels>& terminals) const;

    // Feeds simulation output: for each channel, the voltage between its terminals and the current
    // it delivers at each `time`. All spans have the same length (extra elements are ignored), and
    // time continues from the previous call. Points that do not move time forward are skipped.
    void feed(std::span<const double> time,
              const std::array<std::span<const double>, kChannels>& voltages,
              const std::array<std::span<const double>, kChannels>& currents);

    // The newest reading not yet popped, if a gate has ended since the last call; older unread
    // readings are dropped.
    std::optional<SupplyReading> popReading();

private:
    struct Channel
    {
        double lastVoltage = 0.0;
        double lastCurrent = 0.0;
        double voltageSum = 0.0; // integral over the gate so far
        double currentSum = 0.0;
        bool overLimit = false;
    };

    void accumulate(double t1, const std::array<double, kChannels>& voltages,
                    const std::array<double, kChannels>& currents);
    void finishGate();

    Config config_;
    bool started_ = false;
    long long gateIndex_ = 0; // gates finished since the first point
    double origin_ = 0.0;     // time of the first point
    double lastTime_ = 0.0;
    std::array<Channel, kChannels> channels_{};
    std::optional<SupplyReading> pending_;
};

} // namespace proton::instruments
