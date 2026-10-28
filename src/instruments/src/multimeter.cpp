// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/multimeter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace proton::instruments {

namespace {

using Mode = Multimeter::Mode;
using Range = Multimeter::Range;

bool measuresCurrent(Mode mode)
{
    return mode == Mode::DcCurrent;
}

bool isCurrentRange(Range range)
{
    return range >= Range::Milliamps2;
}

// The fixed ranges of `mode`, lowest and highest.
Range lowestRange(Mode mode)
{
    return measuresCurrent(mode) ? Range::Milliamps2 : Range::Millivolts200;
}

Range highestRange(Mode mode)
{
    return measuresCurrent(mode) ? Range::Amps2 : Range::Volts200;
}

std::string lower(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return result;
}

Range above(Range range)
{
    return static_cast<Range>(static_cast<int>(range) + 1);
}

Range below(Range range)
{
    return static_cast<Range>(static_cast<int>(range) - 1);
}

// formatMeterReading rounds to counts and overloads from 19999.5 counts on.
bool overloads(double value, Range range)
{
    const double count = Multimeter::fullScale(range) / 20000.0;
    return !(std::fabs(value) < (static_cast<double>(core::kMeterCounts) + 0.5) * count);
}

bool underRange(double value, Range range)
{
    const double count = Multimeter::fullScale(range) / 20000.0;
    return std::fabs(value) < static_cast<double>(Multimeter::kDownrangeCounts) * count;
}

} // namespace

Multimeter::Config Multimeter::sanitise(const Config& config)
{
    const Config defaults;
    Config result;
    switch (config.mode) {
    case Mode::DcVoltage:
    case Mode::AcVoltage:
    case Mode::DcCurrent:
        result.mode = config.mode;
        break;
    }
    switch (config.range) {
    case Range::Auto:
    case Range::Millivolts200:
    case Range::Volts2:
    case Range::Volts20:
    case Range::Volts200:
    case Range::Milliamps2:
    case Range::Milliamps20:
    case Range::Milliamps200:
    case Range::Amps2:
        // A range of the other quantity, as when the dial turns from V to A, goes back to Auto.
        if (config.range == Range::Auto ||
            isCurrentRange(config.range) == measuresCurrent(result.mode)) {
            result.range = config.range;
        }
        break;
    }
    const double gateTime = std::isfinite(config.gateTime) ? config.gateTime : defaults.gateTime;
    result.gateTime = std::clamp(gateTime, kMinGateTime, kMaxGateTime);
    return result;
}

Multimeter::Range Multimeter::nextRange(Mode mode, Range current, double readingRange)
{
    const Range lowest = lowestRange(mode);
    const Range highest = highestRange(mode);
    if (current == Range::Auto || isCurrentRange(current) != measuresCurrent(mode)) {
        for (Range range = lowest; range != highest; range = above(range)) {
            if (readingRange == fullScale(range)) {
                return range;
            }
        }
        return highest;
    }
    return current == highest ? lowest : above(current);
}

std::string_view Multimeter::unit(Mode mode)
{
    return measuresCurrent(mode) ? "A" : "V";
}

std::string Multimeter::sourceName(std::string_view name)
{
    return "V" + std::string(name);
}

SpiceFragment Multimeter::toSpice(std::string_view name, std::string_view plus,
                                  std::string_view com) const
{
    SpiceFragment fragment;
    if (!measuresCurrent(config_.mode) || plus.empty() || com.empty() || lower(plus) == lower(com)) {
        return fragment;
    }
    fragment.lines.push_back("* multimeter " + std::string(name) + ", DC current");
    fragment.lines.push_back(sourceName(name) + " " + std::string(plus) + " " + std::string(com) +
                             " DC 0");
    return fragment;
}

double Multimeter::fullScale(Range range)
{
    switch (range) {
    case Range::Millivolts200:
        return 0.2;
    case Range::Volts2:
        return 2.0;
    case Range::Volts20:
        return 20.0;
    case Range::Volts200:
        return 200.0;
    case Range::Milliamps2:
        return 0.002;
    case Range::Milliamps20:
        return 0.02;
    case Range::Milliamps200:
        return 0.2;
    case Range::Amps2:
        return 2.0;
    case Range::Auto:
        break;
    }
    return 0.0;
}

void Multimeter::configure(const Config& config)
{
    config_ = sanitise(config);
    started_ = false;
    gateIndex_ = 0;
    sum1_ = 0.0;
    sum2_ = 0.0;
    autoRange_ = highestRange(config_.mode);
    pending_.reset();
}

void Multimeter::feed(std::span<const double> time, std::span<const double> values)
{
    const std::size_t count = std::min(time.size(), values.size());
    for (std::size_t i = 0; i < count; ++i) {
        const double t = time[i];
        const double v = values[i];
        if (!started_) {
            started_ = true;
            origin_ = t;
            lastTime_ = t;
            lastValue_ = v;
            shift_ = v;
            continue;
        }
        if (!(t > lastTime_)) {
            continue;
        }
        // Gate ends are counted from the origin, so they do not drift over a long run.
        double gateEnd = origin_ + static_cast<double>(gateIndex_ + 1) * config_.gateTime;
        while (t > gateEnd) {
            const double fraction = (gateEnd - lastTime_) / (t - lastTime_);
            const double boundaryValue = lastValue_ + fraction * (v - lastValue_);
            accumulate(lastTime_, lastValue_, gateEnd, boundaryValue);
            lastTime_ = gateEnd;
            lastValue_ = boundaryValue;
            finishGate();
            gateEnd = origin_ + static_cast<double>(gateIndex_ + 1) * config_.gateTime;
        }
        accumulate(lastTime_, lastValue_, t, v);
        lastTime_ = t;
        lastValue_ = v;
        if (t == gateEnd) {
            finishGate();
        }
    }
}

std::optional<DmmReading> Multimeter::popReading()
{
    std::optional<DmmReading> reading = std::move(pending_);
    pending_.reset();
    return reading;
}

void Multimeter::accumulate(double t0, double v0, double t1, double v1)
{
    // Exact integrals of a linear segment from a to b over dt.
    const double dt = t1 - t0;
    const double a = v0 - shift_;
    const double b = v1 - shift_;
    sum1_ += 0.5 * (a + b) * dt;
    sum2_ += (a * a + a * b + b * b) / 3.0 * dt;
}

void Multimeter::finishGate()
{
    const double duration = config_.gateTime;
    const double meanShifted = sum1_ / duration;
    double value = shift_ + meanShifted;
    if (config_.mode == Mode::AcVoltage) {
        value = std::sqrt(std::max(sum2_ / duration - meanShifted * meanShifted, 0.0));
    }

    ++gateIndex_;
    DmmReading reading;
    reading.time = origin_ + static_cast<double>(gateIndex_) * config_.gateTime;
    reading.value = value;
    reading.range = fullScale(selectRange(value));
    reading.display = core::formatMeterReading(value, reading.range, unit(config_.mode));
    pending_ = std::move(reading);

    shift_ = lastValue_;
    sum1_ = 0.0;
    sum2_ = 0.0;
}

Multimeter::Range Multimeter::selectRange(double value)
{
    if (config_.range != Range::Auto) {
        return config_.range;
    }
    const Range lowest = lowestRange(config_.mode);
    const Range highest = highestRange(config_.mode);
    if (!std::isfinite(value)) {
        autoRange_ = highest;
        return autoRange_;
    }
    while (autoRange_ != highest && overloads(value, autoRange_)) {
        autoRange_ = above(autoRange_);
    }
    while (autoRange_ != lowest && underRange(value, autoRange_)) {
        autoRange_ = below(autoRange_);
    }
    return autoRange_;
}

} // namespace proton::instruments
