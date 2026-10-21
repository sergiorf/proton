// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/multimeter.h"
#include "proton/test/test.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <vector>

using proton::instruments::DmmReading;
using proton::instruments::Multimeter;
using Config = Multimeter::Config;
using Mode = Multimeter::Mode;
using Range = Multimeter::Range;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

Config makeConfig(Mode mode, Range range, double gateTime)
{
    Config config;
    config.mode = mode;
    config.range = range;
    config.gateTime = gateTime;
    return config;
}

struct Signal
{
    std::vector<double> time;
    std::vector<double> values;
};

// Samples `f` on [start, end] with steps that vary between 0.25 and 1 times `maxStep`, as a SPICE
// engine's do; the grid is deterministic.
Signal sampleIrregular(const std::function<double(double)>& f, double start, double end,
                       double maxStep)
{
    Signal signal;
    std::uint32_t state = 12345;
    double t = start;
    while (t < end) {
        signal.time.push_back(t);
        signal.values.push_back(f(t));
        state = state * 1664525u + 1013904223u;
        const double fraction = 0.25 + 0.75 * static_cast<double>(state >> 8) / 16777216.0;
        t += maxStep * fraction;
    }
    signal.time.push_back(end);
    signal.values.push_back(f(end));
    return signal;
}

// A square of amplitude `amplitude` around `offset`, starting high, with edges 1 ps wide placed
// exactly on the half periods, and a few irregular points on each plateau.
Signal sampleSquare(double amplitude, double offset, double frequency, double end)
{
    Signal signal;
    const double half = 0.5 / frequency;
    constexpr double kEdge = 1e-12;
    for (long long k = 0; static_cast<double>(k) * half < end; ++k) {
        const double start = static_cast<double>(k) * half;
        const double level = offset + (k % 2 == 0 ? amplitude : -amplitude);
        for (const double at : {0.0, 0.13, 0.5, 0.71}) {
            signal.time.push_back(start + (at == 0.0 ? kEdge : at * half));
            signal.values.push_back(level);
        }
        signal.time.push_back(start + half);
        signal.values.push_back(level);
        if (k == 0) {
            signal.time.insert(signal.time.begin(), 0.0);
            signal.values.insert(signal.values.begin(), level);
        }
    }
    signal.time.back() = end; // exactly, so the last gate ends
    return signal;
}

void feed(Multimeter& meter, const Signal& signal)
{
    meter.feed(signal.time, signal.values);
}

// Feeds a signal in chunks of `chunk` points, as the simulation pump does.
void feedInChunks(Multimeter& meter, const Signal& signal, std::size_t chunk)
{
    for (std::size_t i = 0; i < signal.time.size(); i += chunk) {
        const std::size_t n = std::min(chunk, signal.time.size() - i);
        meter.feed(std::span(signal.time).subspan(i, n), std::span(signal.values).subspan(i, n));
    }
}

// Reads the value of the newest reading, or NaN when there is none, so a failure prints it.
double readValue(Multimeter& meter)
{
    const std::optional<DmmReading> reading = meter.popReading();
    return reading ? reading->value : kNaN;
}

// Steps a DC level per gate of 1 s, each level reached 1 ns into its gate, so every reading is
// the level to within a part in 10^9.
class DcSteps
{
public:
    explicit DcSteps(Multimeter& meter) : meter_(meter) {}

    DmmReading read(double level)
    {
        if (!started_) {
            started_ = true;
            const double t = 0.0;
            meter_.feed(std::span(&t, 1), std::span(&level, 1));
        }
        const double time[] = {gateStart_ + 1e-9, gateStart_ + 1.0};
        const double values[] = {level, level};
        meter_.feed(time, values);
        gateStart_ += 1.0;
        return meter_.popReading().value_or(DmmReading{kNaN, kNaN, kNaN, {}});
    }

private:
    Multimeter& meter_;
    bool started_ = false;
    double gateStart_ = 0.0;
};

} // namespace

TEST(dc_of_a_ramp_is_its_mean_over_each_gate)
{
    Multimeter meter(makeConfig(Mode::DcVoltage, Range::Auto, 0.1));
    feed(meter, sampleIrregular([](double t) { return 1.0 + 2.0 * t; }, 0.0, 0.1, 1e-4));
    CHECK_NEAR(readValue(meter), 1.1, 1e-12);
    feed(meter, sampleIrregular([](double t) { return 1.0 + 2.0 * t; }, 0.1, 0.2, 1e-4));
    CHECK_NEAR(readValue(meter), 1.3, 1e-12);
}

TEST(dc_of_a_sine_is_its_offset)
{
    Multimeter meter(makeConfig(Mode::DcVoltage, Range::Auto, 0.1));
    const auto sine = [](double t) {
        return 0.75 + 2.0 * std::sin(2 * std::numbers::pi * 1e3 * t);
    };
    feed(meter, sampleIrregular(sine, 0.0, 0.1, 1e-6));
    CHECK_NEAR(readValue(meter), 0.75, 1e-7); // the grid is linear between points
}

TEST(ac_true_rms_of_a_sine_is_amplitude_over_sqrt2)
{
    for (const double amplitude : {0.1, 1.0, 5.0}) {
        Multimeter meter(makeConfig(Mode::AcVoltage, Range::Auto, 0.1));
        const auto sine = [amplitude](double t) {
            return amplitude * std::sin(2 * std::numbers::pi * 1e3 * t);
        };
        feed(meter, sampleIrregular(sine, 0.0, 0.1, 1e-6));
        CHECK_NEAR(readValue(meter), amplitude / std::numbers::sqrt2, amplitude * 1e-5);
    }
}

TEST(ac_removes_the_dc_part)
{
    for (const double offset : {0.0, 3.0, -100.0}) {
        Multimeter meter(makeConfig(Mode::AcVoltage, Range::Auto, 0.1));
        const auto sine = [offset](double t) {
            return offset + std::sin(2 * std::numbers::pi * 1e3 * t);
        };
        feed(meter, sampleIrregular(sine, 0.0, 0.1, 1e-6));
        CHECK_NEAR(readValue(meter), 1.0 / std::numbers::sqrt2, 1e-5);
    }
}

TEST(ac_true_rms_of_a_square_is_its_amplitude)
{
    Multimeter meter(makeConfig(Mode::AcVoltage, Range::Auto, 0.1));
    feed(meter, sampleSquare(2.0, 1.0, 1e3, 0.1));
    CHECK_NEAR(readValue(meter), 2.0, 1e-6);
}

TEST(one_reading_per_gate_stamped_with_the_gate_end)
{
    Multimeter meter(makeConfig(Mode::DcVoltage, Range::Auto, 0.01));
    CHECK(!meter.popReading().has_value());
    feed(meter, sampleIrregular([](double) { return 1.0; }, 0.5, 0.505, 1e-4));
    CHECK(!meter.popReading().has_value()); // half a gate
    feed(meter, sampleIrregular([](double) { return 1.0; }, 0.505, 0.515, 1e-4));
    const std::optional<DmmReading> reading = meter.popReading();
    CHECK(reading.has_value());
    CHECK_NEAR(reading.value_or(DmmReading{}).time, 0.51, 1e-15);
    CHECK(!meter.popReading().has_value()); // popped
}

TEST(only_the_newest_reading_is_kept)
{
    Multimeter meter(makeConfig(Mode::DcVoltage, Range::Auto, 0.01));
    feed(meter, sampleIrregular([](double t) { return t; }, 0.0, 0.035, 1e-4));
    const std::optional<DmmReading> reading = meter.popReading();
    CHECK_NEAR(reading.value_or(DmmReading{}).time, 0.03, 1e-15);
    CHECK_NEAR(reading.value_or(DmmReading{}).value, 0.025, 1e-12);
    CHECK(!meter.popReading().has_value());
}

TEST(a_segment_across_a_gate_end_is_split_there)
{
    // One straight line from (0, 0) to (0.15, 1.5): the first gate holds v = 10 t on [0, 0.1].
    Multimeter meter(makeConfig(Mode::DcVoltage, Range::Auto, 0.1));
    const double time[] = {0.0, 0.15};
    const double values[] = {0.0, 1.5};
    meter.feed(time, values);
    CHECK_NEAR(readValue(meter), 0.5, 1e-12);
    const double end[] = {0.2};
    const double endValue[] = {2.0};
    meter.feed(end, endValue);
    CHECK_NEAR(readValue(meter), 1.5, 1e-12);
}

TEST(chunking_does_not_change_readings)
{
    const auto sine = [](double t) { return 0.2 + std::sin(2 * std::numbers::pi * 50.0 * t); };
    const Signal signal = sampleIrregular(sine, 0.0, 0.11, 1e-5);
    for (const Mode mode : {Mode::DcVoltage, Mode::AcVoltage}) {
        Multimeter whole(makeConfig(mode, Range::Auto, 0.02));
        Multimeter chunked(makeConfig(mode, Range::Auto, 0.02));
        feed(whole, signal);
        feedInChunks(chunked, signal, 37);
        CHECK_EQ(readValue(chunked), readValue(whole));
    }
}

TEST(points_that_do_not_move_time_forward_are_skipped)
{
    Multimeter meter(makeConfig(Mode::DcVoltage, Range::Auto, 0.1));
    const double time[] = {0.0, 0.05, 0.05, 0.04, kNaN, 0.1};
    const double values[] = {1.0, 1.0, 50.0, 50.0, 50.0, 1.0};
    meter.feed(time, values);
    CHECK_NEAR(readValue(meter), 1.0, 1e-15);
}

TEST(fixed_range_shows_the_reading_on_that_range)
{
    Multimeter meter(makeConfig(Mode::DcVoltage, Range::Volts2, 1.0));
    DcSteps steps(meter);
    DmmReading reading = steps.read(0.1234);
    CHECK_EQ(reading.range, 2.0);
    CHECK_EQ(reading.display.text, "0.1234");
    CHECK_EQ(reading.display.unit, "V");
    reading = steps.read(25.0);
    CHECK_EQ(reading.range, 2.0);
    CHECK(reading.display.overload);
    CHECK_EQ(reading.display.text, "OL");
}

TEST(auto_range_picks_the_lowest_range_that_fits)
{
    struct Case
    {
        double level;
        double range;
        std::string text;
        std::string unit;
    };
    const Case cases[] = {
        {0.1, 0.2, "100.00", "mV"},  {-0.05, 0.2, "-50.00", "mV"},  {1.5, 2.0, "1.5000", "V"},
        {15.0, 20.0, "15.000", "V"}, {150.0, 200.0, "150.00", "V"}, {500.0, 200.0, "OL", "V"},
        {0.0, 0.2, "0.00", "mV"},
    };
    for (const Case& c : cases) {
        Multimeter meter(makeConfig(Mode::DcVoltage, Range::Auto, 1.0));
        DcSteps steps(meter);
        const DmmReading reading = steps.read(c.level);
        CHECK_EQ(reading.range, c.range);
        CHECK_EQ(reading.display.text, c.text);
        CHECK_EQ(reading.display.unit, c.unit);
    }
}

TEST(auto_range_changes_range_at_the_thresholds)
{
    Multimeter meter(makeConfig(Mode::DcVoltage, Range::Auto, 1.0));
    DcSteps steps(meter);
    CHECK_EQ(steps.read(0.1).range, 0.2);
    CHECK_EQ(steps.read(0.1999).range, 0.2);  // 19990 counts
    CHECK_EQ(steps.read(0.19999).range, 0.2); // 19999 counts, the last before overload
    CHECK_EQ(steps.read(0.2).range, 2.0);     // overload: up
    CHECK_EQ(steps.read(0.19).range, 2.0);    // 1900 counts: above the downrange threshold
    CHECK_EQ(steps.read(0.1801).range, 2.0);  // 1801 counts
    CHECK_EQ(steps.read(0.1799).range, 0.2);  // 1799 counts: down
    CHECK_EQ(steps.read(1.9999).range, 2.0);
    CHECK_EQ(steps.read(1.99996).range, 20.0); // rounds to 20000 counts: up
    CHECK_EQ(steps.read(1.0).range, 2.0);      // 1000 counts on 20 V: down
    CHECK_EQ(steps.read(100.0).range, 200.0);  // several ranges at once
    CHECK_EQ(steps.read(0.001).range, 0.2);
}

TEST(configure_restarts_acquisition)
{
    Multimeter meter(makeConfig(Mode::DcVoltage, Range::Auto, 0.1));
    feed(meter, sampleIrregular([](double) { return 5.0; }, 0.0, 0.15, 1e-3));
    meter.configure(makeConfig(Mode::DcVoltage, Range::Auto, 0.1));
    CHECK(!meter.popReading().has_value()); // the unread reading is dropped
    // The gate restarts at the next point fed; the 5 V already integrated is gone.
    feed(meter, sampleIrregular([](double) { return 1.0; }, 0.15, 0.2, 1e-3));
    CHECK(!meter.popReading().has_value());
    feed(meter, sampleIrregular([](double) { return 1.0; }, 0.2, 0.26, 1e-3));
    const std::optional<DmmReading> reading = meter.popReading();
    CHECK_NEAR(reading.value_or(DmmReading{}).value, 1.0, 1e-15);
    CHECK_NEAR(reading.value_or(DmmReading{}).time, 0.25, 1e-15);
}

TEST(default_config_is_auto_range_dc_with_a_100ms_gate)
{
    const Multimeter meter;
    CHECK_EQ(meter.config().mode, Mode::DcVoltage);
    CHECK_EQ(meter.config().range, Range::Auto);
    CHECK_EQ(meter.config().gateTime, 0.1);
}

TEST(settings_are_sanitised)
{
    const auto gate = [](double value) {
        return Multimeter::sanitise(makeConfig(Mode::DcVoltage, Range::Auto, value)).gateTime;
    };
    CHECK_EQ(gate(0.5), 0.5);
    CHECK_EQ(gate(0.0), Multimeter::kMinGateTime);
    CHECK_EQ(gate(-1.0), Multimeter::kMinGateTime);
    CHECK_EQ(gate(100.0), Multimeter::kMaxGateTime);
    CHECK_EQ(gate(kNaN), 0.1);

    const Config odd = makeConfig(static_cast<Mode>(7), static_cast<Range>(9), 0.1);
    CHECK_EQ(Multimeter::sanitise(odd).mode, Mode::DcVoltage);
    CHECK_EQ(Multimeter::sanitise(odd).range, Range::Auto);

    Multimeter meter;
    meter.configure(makeConfig(Mode::AcVoltage, Range::Volts20, -3.0));
    CHECK_EQ(meter.config().mode, Mode::AcVoltage);
    CHECK_EQ(meter.config().range, Range::Volts20);
    CHECK_EQ(meter.config().gateTime, Multimeter::kMinGateTime);
}

TEST(full_scale_of_each_range)
{
    CHECK_EQ(Multimeter::fullScale(Range::Millivolts200), 0.2);
    CHECK_EQ(Multimeter::fullScale(Range::Volts2), 2.0);
    CHECK_EQ(Multimeter::fullScale(Range::Volts20), 20.0);
    CHECK_EQ(Multimeter::fullScale(Range::Volts200), 200.0);
    CHECK_EQ(Multimeter::fullScale(Range::Auto), 0.0);
}
