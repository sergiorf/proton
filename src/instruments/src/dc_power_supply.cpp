// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/dc_power_supply.h"

#include "proton/core/spice_number.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

namespace proton::instruments {

namespace {

double finiteOr(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

// `value` with `decimals` decimals, or "OL" from `overload` on. Locale-free; never "-0.000".
std::string fixed(double value, int decimals, double overload)
{
    const double scale = std::pow(10.0, decimals);
    double rounded = std::round(value * scale) / scale;
    if (!std::isfinite(rounded) || std::fabs(rounded) >= overload) {
        return "OL";
    }
    if (rounded == 0.0) {
        rounded = 0.0; // drops the sign of -0
    }
    std::array<char, 32> buffer{};
    const auto end = std::to_chars(buffer.data(), buffer.data() + buffer.size(), rounded,
                                   std::chars_format::fixed, decimals)
                         .ptr;
    return {buffer.data(), end};
}

} // namespace

DcPowerSupply::Config DcPowerSupply::sanitise(const Config& config)
{
    const ChannelConfig defaults;
    Config result;
    for (std::size_t c = 0; c < kChannels; ++c) {
        const ChannelConfig& in = config.channels[c];
        ChannelConfig& out = result.channels[c];
        out.voltage = std::clamp(finiteOr(in.voltage, defaults.voltage), 0.0, kMaxVoltage);
        out.currentLimit = std::clamp(finiteOr(in.currentLimit, defaults.currentLimit),
                                      kMinCurrentLimit, kMaxCurrentLimit);
        out.output = in.output;
    }
    return result;
}

std::string DcPowerSupply::sourceName(std::string_view name, std::size_t channel)
{
    return "V" + std::string(name) + "_" + std::to_string(channel + 1);
}

std::string DcPowerSupply::formatVoltage(double volts)
{
    return fixed(volts, 3, 100.0);
}

std::string DcPowerSupply::formatCurrent(double amps)
{
    return fixed(amps, 4, 10.0);
}

void DcPowerSupply::configure(const Config& config)
{
    config_ = sanitise(config);
    started_ = false;
    gateIndex_ = 0;
    channels_ = {};
    pending_.reset();
}

SpiceFragment DcPowerSupply::toSpice(std::string_view name,
                                     const std::array<Terminals, kChannels>& terminals) const
{
    const std::string instance(name);
    SpiceFragment fragment;
    for (std::size_t c = 0; c < kChannels; ++c) {
        const Terminals& t = terminals[c];
        if (t.plus.empty() || t.minus.empty()) {
            continue;
        }
        if (fragment.lines.empty()) {
            fragment.lines.push_back("* DC power supply " + instance);
        }
        const ChannelConfig& channel = config_.channels[c];
        const std::string source = sourceName(instance, c);
        if (!channel.output) {
            const std::string internal = instance + "_" + std::to_string(c + 1) + "_int";
            fragment.lines.push_back(source + " " + internal + " " + t.minus + " DC 0");
            fragment.lines.push_back("R" + source.substr(1) + " " + internal + " " + t.plus + " " +
                                     core::formatSpiceNumber(kOffResistance));
            continue;
        }
        fragment.lines.push_back(source + " " + t.plus + " " + t.minus + " DC " +
                                 core::formatSpiceNumber(channel.voltage));
    }
    return fragment;
}

void DcPowerSupply::feed(std::span<const double> time,
                         const std::array<std::span<const double>, kChannels>& voltages,
                         const std::array<std::span<const double>, kChannels>& currents)
{
    std::size_t count = time.size();
    for (std::size_t c = 0; c < kChannels; ++c) {
        count = std::min({count, voltages[c].size(), currents[c].size()});
    }
    for (std::size_t i = 0; i < count; ++i) {
        const double t = time[i];
        std::array<double, kChannels> v{};
        std::array<double, kChannels> a{};
        for (std::size_t c = 0; c < kChannels; ++c) {
            v[c] = voltages[c][i];
            a[c] = currents[c][i];
        }
        if (!started_) {
            started_ = true;
            origin_ = t;
            lastTime_ = t;
            for (std::size_t c = 0; c < kChannels; ++c) {
                channels_[c].lastVoltage = v[c];
                channels_[c].lastCurrent = a[c];
                channels_[c].overLimit = std::fabs(a[c]) > config_.channels[c].currentLimit;
            }
            continue;
        }
        if (!(t > lastTime_)) {
            continue;
        }
        // Gate ends are counted from the origin, so they do not drift over a long run.
        double gateEnd = origin_ + static_cast<double>(gateIndex_ + 1) * kGateTime;
        while (t > gateEnd) {
            const double fraction = (gateEnd - lastTime_) / (t - lastTime_);
            std::array<double, kChannels> vb{};
            std::array<double, kChannels> ab{};
            for (std::size_t c = 0; c < kChannels; ++c) {
                vb[c] = channels_[c].lastVoltage + fraction * (v[c] - channels_[c].lastVoltage);
                ab[c] = channels_[c].lastCurrent + fraction * (a[c] - channels_[c].lastCurrent);
            }
            accumulate(gateEnd, vb, ab);
            finishGate();
            gateEnd = origin_ + static_cast<double>(gateIndex_ + 1) * kGateTime;
        }
        accumulate(t, v, a);
        // Linear between points, so the largest current of a segment is at one of its ends.
        for (std::size_t c = 0; c < kChannels; ++c) {
            if (std::fabs(a[c]) > config_.channels[c].currentLimit) {
                channels_[c].overLimit = true;
            }
        }
        if (t == gateEnd) {
            finishGate();
        }
    }
}

std::optional<SupplyReading> DcPowerSupply::popReading()
{
    std::optional<SupplyReading> reading = std::move(pending_);
    pending_.reset();
    return reading;
}

void DcPowerSupply::accumulate(double t1, const std::array<double, kChannels>& voltages,
                               const std::array<double, kChannels>& currents)
{
    const double dt = t1 - lastTime_;
    for (std::size_t c = 0; c < kChannels; ++c) {
        Channel& channel = channels_[c];
        channel.voltageSum += 0.5 * (channel.lastVoltage + voltages[c]) * dt;
        channel.currentSum += 0.5 * (channel.lastCurrent + currents[c]) * dt;
        channel.lastVoltage = voltages[c];
        channel.lastCurrent = currents[c];
    }
    lastTime_ = t1;
}

void DcPowerSupply::finishGate()
{
    ++gateIndex_;
    SupplyReading reading;
    reading.time = origin_ + static_cast<double>(gateIndex_) * kGateTime;
    for (std::size_t c = 0; c < kChannels; ++c) {
        Channel& channel = channels_[c];
        SupplyChannelReading& out = reading.channels[c];
        out.voltage = channel.voltageSum / kGateTime;
        out.current = channel.currentSum / kGateTime;
        out.overLimit = channel.overLimit;
        out.voltageText = formatVoltage(out.voltage);
        out.currentText = formatCurrent(out.current);
        channel.voltageSum = 0.0;
        channel.currentSum = 0.0;
        // The next gate starts at the boundary point, which counts towards it as well.
        channel.overLimit = std::fabs(channel.lastCurrent) > config_.channels[c].currentLimit;
    }
    pending_ = std::move(reading);
}

} // namespace proton::instruments
