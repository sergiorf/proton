// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/scope_front_end.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>
#include <utility>

namespace proton::instruments {

static_assert(std::tuple_size_v<decltype(ScopeSamples::values)> == ScopeFrontEnd::kChannels);

namespace {

using Coupling = ScopeFrontEnd::Coupling;

double finiteOr(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

double timeConstant(double corner)
{
    return 1.0 / (2.0 * std::numbers::pi * corner);
}

// Advances the error state e = z - x of a one-pole low-pass z' = (x - z) / tau over a step of
// `dt` in which x rises linearly by `dx`. Exact: e' = -e / tau - dx / dt.
double decay(double e, double dt, double dx, double tau)
{
    const double r = dt / tau;
    const double kept = std::exp(-r);
    const double gain = r > 0.0 ? -std::expm1(-r) / r : 1.0; // (1 - kept) / r, also for tiny r
    return e * kept - dx * gain;
}

} // namespace

ScopeFrontEnd::Config ScopeFrontEnd::sanitise(const Config& config)
{
    const Config defaults;
    const ChannelConfig channelDefaults;
    Config result;
    result.timePerDiv = std::clamp(finiteOr(config.timePerDiv, defaults.timePerDiv),
                                   kMinTimePerDiv, kMaxTimePerDiv);
    for (std::size_t i = 0; i < kChannels; ++i) {
        const ChannelConfig& in = config.channels[i];
        ChannelConfig& out = result.channels[i];
        out.voltsPerDiv = std::clamp(finiteOr(in.voltsPerDiv, channelDefaults.voltsPerDiv),
                                     kMinVoltsPerDiv, kMaxVoltsPerDiv);
        out.offset = std::clamp(finiteOr(in.offset, channelDefaults.offset), -kMaxOffset,
                                kMaxOffset);
        switch (in.coupling) {
        case Coupling::Dc:
        case Coupling::Ac:
        case Coupling::Gnd:
            out.coupling = in.coupling;
            break;
        }
        out.bandwidthLimit = in.bandwidthLimit;
        out.bandwidth = std::clamp(finiteOr(in.bandwidth, channelDefaults.bandwidth),
                                   kMinBandwidth, kMaxBandwidth);
        out.quantise = in.quantise;
        out.adcBits = std::clamp(in.adcBits, kMinAdcBits, kMaxAdcBits);
        out.shown = in.shown;
    }
    return result;
}

void ScopeFrontEnd::configure(const Config& config)
{
    config_ = sanitise(config);
    interval_ = static_cast<double>(kHorizontalDivisions) * config_.timePerDiv /
                static_cast<double>(kSamplesPerScreen);
    for (std::size_t i = 0; i < kChannels; ++i) {
        const ChannelConfig& c = config_.channels[i];
        Channel& channel = channels_[i];
        channel.tauAc = timeConstant(kAcCouplingCorner);
        channel.tauBandwidth = timeConstant(c.bandwidth);
        channel.lsb = c.quantise ? static_cast<double>(kVerticalDivisions) * c.voltsPerDiv /
                                       std::ldexp(1.0, c.adcBits)
                                 : 0.0;
        channel.lastValue = 0.0;
        channel.state = FilterState{};
    }
    started_ = false;
    nextSample_ = 0;
    pending_ = ScopeSamples{};
}

void ScopeFrontEnd::feed(std::span<const double> time, std::span<const double> channel1,
                         std::span<const double> channel2)
{
    const std::array<std::span<const double>, kChannels> inputs{channel1, channel2};
    std::size_t count = time.size();
    for (const std::span<const double>& input : inputs) {
        if (!input.empty()) {
            count = std::min(count, input.size());
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const double t = time[i];
        if (!std::isfinite(t) || (started_ && !(t > lastTime_))) {
            continue;
        }
        std::array<double, kChannels> x{};
        for (std::size_t c = 0; c < kChannels; ++c) {
            x[c] = inputs[c].empty() ? 0.0 : inputs[c][i];
        }
        if (started_) {
            step(t, x);
            continue;
        }
        // The first point: the filters start in steady state on it, and it is sample 0.
        started_ = true;
        origin_ = t;
        lastTime_ = t;
        for (std::size_t c = 0; c < kChannels; ++c) {
            channels_[c].lastValue = x[c];
            channels_[c].state = FilterState{};
            pending_.values[c].push_back(sample(c, x[c], FilterState{}));
        }
        nextSample_ = 1;
    }
}

ScopeSamples ScopeFrontEnd::takeSamples()
{
    ScopeSamples samples = std::move(pending_);
    pending_ = ScopeSamples{};
    pending_.first = nextSample_;
    return samples;
}

ScopeFrontEnd::FilterState ScopeFrontEnd::advance(std::size_t index, double dt, double dx) const
{
    const ChannelConfig& c = config_.channels[index];
    const Channel& channel = channels_[index];
    FilterState state = channel.state;
    if (c.coupling == Coupling::Ac) {
        state.ac = decay(state.ac, dt, dx, channel.tauAc);
    }
    if (c.bandwidthLimit) {
        state.bandwidth = decay(state.bandwidth, dt, dx, channel.tauBandwidth);
    }
    return state;
}

double ScopeFrontEnd::sample(std::size_t index, double x, const FilterState& state) const
{
    const ChannelConfig& c = config_.channels[index];
    const Channel& channel = channels_[index];

    // Analog front end. With z_a and z_b the low-passes of x at the AC and bandwidth corners:
    // AC coupling is x - z_a = -e_a, the bandwidth limit is z_b = x + e_b, and both together are
    // z_b - (low-pass b of z_a) = a (e_b - e_a) / (a - b) by partial fractions, with a and b the
    // time constants (b is at most a hundredth of a, so they never meet).
    double y = x;
    if (c.coupling == Coupling::Gnd) {
        y = 0.0; // the input is grounded
    } else if (c.coupling == Coupling::Ac && c.bandwidthLimit) {
        const double a = channel.tauAc;
        const double b = channel.tauBandwidth;
        y = a * (state.bandwidth - state.ac) / (a - b);
    } else if (c.coupling == Coupling::Ac) {
        y = -state.ac;
    } else if (c.bandwidthLimit) {
        y = x + state.bandwidth;
    }

    // ADC: the screen spans kVerticalDivisions divisions centred on the offset.
    const double halfScreen = 0.5 * static_cast<double>(kVerticalDivisions) * c.voltsPerDiv;
    if (channel.lsb > 0.0) {
        const double halfCodes = std::ldexp(1.0, c.adcBits - 1);
        const double code = std::clamp(std::round((y - c.offset) / channel.lsb), -halfCodes,
                                       halfCodes - 1.0);
        return c.offset + code * channel.lsb;
    }
    return std::clamp(y, c.offset - halfScreen, c.offset + halfScreen);
}

void ScopeFrontEnd::step(double t, const std::array<double, kChannels>& x)
{
    const double t0 = lastTime_;
    // Every sample in (t0, t], evaluated exactly from the state at t0.
    for (double ts = sampleTime(nextSample_); ts <= t; ts = sampleTime(++nextSample_)) {
        const double fraction = (ts - t0) / (t - t0);
        for (std::size_t c = 0; c < kChannels; ++c) {
            const Channel& channel = channels_[c];
            const double xs =
                ts == t ? x[c] : channel.lastValue + fraction * (x[c] - channel.lastValue);
            const FilterState state = advance(c, ts - t0, xs - channel.lastValue);
            pending_.values[c].push_back(sample(c, xs, state));
        }
    }
    for (std::size_t c = 0; c < kChannels; ++c) {
        Channel& channel = channels_[c];
        channel.state = advance(c, t - t0, x[c] - channel.lastValue);
        channel.lastValue = x[c];
    }
    lastTime_ = t;
}

} // namespace proton::instruments
