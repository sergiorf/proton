// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/multimeter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace proton::instruments {

namespace {

using Range = Multimeter::Range;

constexpr Range kLowestRange = Range::Millivolts200;
constexpr Range kHighestRange = Range::Volts200;

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
        result.mode = config.mode;
        break;
    }
    switch (config.range) {
    case Range::Auto:
    case Range::Millivolts200:
    case Range::Volts2:
    case Range::Volts20:
    case Range::Volts200:
        result.range = config.range;
        break;
    }
    const double gateTime = std::isfinite(config.gateTime) ? config.gateTime : defaults.gateTime;
    result.gateTime = std::clamp(gateTime, kMinGateTime, kMaxGateTime);
    return result;
}

Multimeter::Range Multimeter::nextRange(Range current, double readingRange)
{
    switch (current) {
    case Range::Auto:
        for (const Range range : {Range::Millivolts200, Range::Volts2, Range::Volts20}) {
            if (readingRange == fullScale(range)) {
                return range;
            }
        }
        return Range::Volts200;
    case Range::Millivolts200:
        return Range::Volts2;
    case Range::Volts2:
        return Range::Volts20;
    case Range::Volts20:
        return Range::Volts200;
    case Range::Volts200:
        return Range::Millivolts200;
    }
    return Range::Volts200;
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
    autoRange_ = kHighestRange;
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
    reading.display = core::formatMeterReading(value, reading.range, "V");
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
    if (!std::isfinite(value)) {
        autoRange_ = kHighestRange;
        return autoRange_;
    }
    while (autoRange_ != kHighestRange && overloads(value, autoRange_)) {
        autoRange_ = above(autoRange_);
    }
    while (autoRange_ != kLowestRange && underRange(value, autoRange_)) {
        autoRange_ = below(autoRange_);
    }
    return autoRange_;
}

} // namespace proton::instruments
