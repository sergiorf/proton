// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/scope.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace proton::instruments {

namespace {

constexpr long long kRecord = Scope::kSamplesPerScreen;
constexpr long long kHalf = kRecord / 2;

double finiteOr(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

} // namespace

Scope::Config Scope::sanitise(const Config& config)
{
    const Config defaults;
    Config result;

    ScopeFrontEnd::Config frontEnd;
    frontEnd.timePerDiv = config.timePerDiv;
    frontEnd.channels = config.channels;
    frontEnd = ScopeFrontEnd::sanitise(frontEnd);
    result.timePerDiv = frontEnd.timePerDiv;
    result.channels = frontEnd.channels;

    const Trigger& in = config.trigger;
    Trigger& out = result.trigger;
    switch (in.source) {
    case TriggerSource::Channel1:
    case TriggerSource::Channel2:
        out.source = in.source;
        break;
    }
    out.level =
        std::clamp(finiteOr(in.level, defaults.trigger.level), -kMaxTriggerLevel, kMaxTriggerLevel);
    switch (in.slope) {
    case Slope::Rising:
    case Slope::Falling:
        out.slope = in.slope;
        break;
    }
    out.hysteresis =
        std::clamp(finiteOr(in.hysteresis, defaults.trigger.hysteresis), 0.0, kMaxHysteresis);

    switch (config.mode) {
    case Mode::Auto:
    case Mode::Normal:
    case Mode::Single:
        result.mode = config.mode;
        break;
    }
    result.deadTime = std::clamp(finiteOr(config.deadTime, defaults.deadTime), 0.0, kMaxDeadTime);
    return result;
}

void Scope::configure(const Config& config)
{
    config_ = sanitise(config);

    ScopeFrontEnd::Config frontEnd;
    frontEnd.timePerDiv = config_.timePerDiv;
    frontEnd.channels = config_.channels;
    frontEnd_.configure(frontEnd);

    const auto source = static_cast<std::size_t>(config_.trigger.source);
    hysteresis_ = config_.trigger.hysteresis * config_.channels[source].voltsPerDiv;
    deadSamples_ = static_cast<long long>(std::ceil(config_.deadTime / sampleInterval()));

    state_ = State::Waiting;
    next_ = 0;
    acquisitionStart_ = 0;
    armed_ = false;
    for (std::vector<double>& history : history_) {
        history.clear();
    }
    pending_.reset();
}

void Scope::feed(std::span<const double> time, std::span<const double> channel1,
                 std::span<const double> channel2)
{
    frontEnd_.feed(time, channel1, channel2);
    const ScopeSamples samples = frontEnd_.takeSamples();
    const std::size_t count = samples.values[0].size();
    for (std::size_t i = 0; i < count && state_ != State::Stopped; ++i) {
        process({samples.values[0][i], samples.values[1][i]});
    }
}

std::optional<ScopeFrame> Scope::popFrame()
{
    return std::exchange(pending_, std::nullopt);
}

void Scope::process(const std::array<double, kChannels>& values)
{
    const long long index = next_++;
    for (std::size_t c = 0; c < kChannels; ++c) {
        std::vector<double>& history = history_[c];
        if (history.size() >= static_cast<std::size_t>(2 * kRecord)) {
            history.erase(history.begin(), history.end() - (kRecord - 1));
        }
        history.push_back(values[c]);
    }
    if (state_ == State::Waiting && index >= acquisitionStart_) {
        search(index);
    } else if (state_ == State::Recording && index == triggerIndex_ + kHalf) {
        finishRecord(index);
    }
}

void Scope::search(long long index)
{
    // The trigger arms once the pre-trigger half of the record is full.
    const long long armFrom = acquisitionStart_ + kHalf - 1;
    if (index < armFrom) {
        return;
    }
    // A falling edge is a rising edge of the negated signal.
    const Trigger& trigger = config_.trigger;
    const double sign = trigger.slope == Slope::Rising ? 1.0 : -1.0;
    const std::vector<double>& source = history_[static_cast<std::size_t>(trigger.source)];
    const double level = sign * trigger.level;
    const double value = sign * source.back();
    if (armed_ && value >= level) {
        // Armed, so the sample before is below the level and in this acquisition.
        const double previous = sign * source[source.size() - 2];
        startRecord(index - 1, (level - previous) / (value - previous), true);
        return;
    }
    if (value < level - hysteresis_) {
        armed_ = true;
    }
    if (config_.mode == Mode::Auto && index >= armFrom + kAutoTimeoutScreens * kRecord) {
        startRecord(index, 0.0, false);
    }
}

void Scope::startRecord(long long index, double fraction, bool triggered)
{
    state_ = State::Recording;
    triggerIndex_ = index;
    triggerFraction_ = fraction;
    triggered_ = triggered;
}

void Scope::finishRecord(long long index)
{
    // The record is the last kRecord samples: from triggerIndex_ - kHalf + 1 to index.
    ScopeFrame frame;
    const double interval = sampleInterval();
    frame.triggerTime = frontEnd_.sampleTime(triggerIndex_) + triggerFraction_ * interval;
    frame.triggered = triggered_;
    frame.startTime = (1.0 - triggerFraction_ - static_cast<double>(kHalf)) * interval;
    frame.sampleInterval = interval;
    for (std::size_t c = 0; c < kChannels; ++c) {
        const std::vector<double>& history = history_[c];
        frame.values[c].assign(history.end() - kRecord, history.end());
    }
    pending_ = std::move(frame);

    if (config_.mode == Mode::Single) {
        state_ = State::Stopped;
        return;
    }
    state_ = State::Waiting;
    acquisitionStart_ = index + 1 + deadSamples_;
    armed_ = false;
}

} // namespace proton::instruments
