// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/core/format.h"
#include "proton/instruments/spice_fragment.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace proton::instruments {

// One reading of the multimeter, taken at the end of a gate.
struct DmmReading
{
    double time = 0.0;          // simulation time at the end of the gate, s
    double value = 0.0;         // the measured value, unrounded, V or A
    double range = 0.0;         // full scale of the range it was read on, V or A
    core::MeterDisplay display; // what the 4.5-digit display shows
};

// A 4.5-digit (19999-count) bench multimeter, ideal (ARCHITECTURE.md section 6): no input
// loading, no burden voltage, no noise. It is fed one signal: the voltage between its `+` and
// `COM` in the voltage modes, the current through it from `+` to `COM` in the current mode.
//
// The meter integrates the signal over a gate of `gateTime` seconds of simulation time and gives
// one reading per gate. The signal is taken as linear between the points it is fed, so the
// integrals are exact for any time grid, regular or not:
//
// - DC V and DC A are the mean over the gate.
// - AC V is the true RMS with the mean removed (an AC-coupled true-RMS meter): a sine of
//   amplitude A reads A/sqrt(2) and a square A, whatever their offset. Gates that hold a whole
//   number of periods read exactly; others read close.
//
// In DC A the meter is an ammeter, wired in series: toSpice() puts a 0 V source between its
// terminals, whose branch current is what it reads (PLAN.md M5.2). In the voltage modes it adds
// nothing to the circuit.
//
// Gates are counted from the first point fed after configure(); a segment that crosses the end of
// a gate is split there. Voltage ranges are 200 mV, 2 V, 20 V and 200 V; current ranges 2 mA,
// 20 mA, 200 mA and 2 A. On a fixed range a reading beyond 19999 counts shows the overload.
// Auto-range picks the range of the mode for each reading: up while the reading overloads the
// range, down while it is below kDownrangeCounts of the range, so a reading between the two
// thresholds stays on the range it was on.
//
// Settings are sanitised: an unknown mode becomes the default, a range that is unknown or belongs
// to the other quantity becomes Auto, and the gate time is clamped to [kMinGateTime,
// kMaxGateTime] (the default when not finite).
class Multimeter
{
public:
    enum class Mode {
        DcVoltage,
        AcVoltage,
        DcCurrent,
    };

    enum class Range {
        Auto,
        Millivolts200,
        Volts2,
        Volts20,
        Volts200,
        Milliamps2,
        Milliamps20,
        Milliamps200,
        Amps2,
    };

    static constexpr double kMinGateTime = 1e-3; // s
    static constexpr double kMaxGateTime = 10.0; // s
    // Auto-range goes down below this many counts: 9 % of full scale, so the reading lands at
    // most at 18000 counts on the range below, under its 19999-count overload.
    static constexpr long long kDownrangeCounts = 1800;

    struct Config
    {
        Mode mode = Mode::DcVoltage;
        Range range = Range::Auto;
        double gateTime = 0.1; // s of simulation time

        friend bool operator==(const Config&, const Config&) = default;
    };

    // The settings as the meter would apply them.
    static Config sanitise(const Config& config);

    // Full scale of a fixed range in volts (0.2, 2, 20 or 200) or amps (0.002, 0.02, 0.2 or 2).
    // Auto has none (0).
    static double fullScale(Range range);

    // The unit of what `mode` measures: "V" or "A".
    static std::string_view unit(Mode mode);

    // What a meter's RANGE button does in `mode`: from Auto it holds the range the last reading
    // was on (`readingRange`, its full scale; the highest range of the mode when there is none),
    // and on a fixed range it goes to the next one up, wrapping from the highest to the lowest.
    static Range nextRange(Mode mode, Range current, double readingRange);

    Multimeter() = default;
    explicit Multimeter(const Config& config) { configure(config); }

    // Applies sanitised settings and restarts acquisition: the gate in progress and any unread
    // reading are dropped, and auto-range starts again from the highest range.
    void configure(const Config& config);
    const Config& config() const { return config_; }

    // The meter as SPICE elements named after the instance name `name` ("dmm1"), between the nets
    // of its `+` and `COM`. In DC A, a 0 V source from + to COM, so its branch current, "i(vdmm1)",
    // is the current through the meter from + to COM:
    //
    //     * multimeter dmm1, DC current
    //     Vdmm1 a b DC 0
    //
    // Nothing in the voltage modes, or when the two terminals are on the same net (ignoring case):
    // a meter shorted by its own leads reads 0 A. Deterministic.
    SpiceFragment toSpice(std::string_view name, std::string_view plus, std::string_view com) const;

    // The SPICE name of instance `name`'s ammeter source: "Vdmm1".
    static std::string sourceName(std::string_view name);

    // Feeds simulation output: `values[i]` is the measured voltage or current at `time[i]`. Both spans have the
    // same length (extra elements of the longer one are ignored), and time continues from the
    // previous call. Points that do not move time forward are skipped.
    void feed(std::span<const double> time, std::span<const double> values);

    // The newest reading not yet popped, if any gate has ended since the last call. Like a
    // display, the meter keeps only the newest: older unread readings are dropped.
    std::optional<DmmReading> popReading();

private:
    void accumulate(double t0, double v0, double t1, double v1);
    void finishGate();
    Range selectRange(double value);

    Config config_;
    bool started_ = false;
    long long gateIndex_ = 0; // gates finished since the first point
    double origin_ = 0.0;     // time of the first point
    double lastTime_ = 0.0;
    double lastValue_ = 0.0;
    // Integrals over the gate of (v - shift_) and (v - shift_)^2; the shift is the first value of
    // the gate, which keeps the variance free of cancellation when the DC part is large.
    double shift_ = 0.0;
    double sum1_ = 0.0;
    double sum2_ = 0.0;
    Range autoRange_ = Range::Volts200;
    std::optional<DmmReading> pending_;
};

} // namespace proton::instruments
