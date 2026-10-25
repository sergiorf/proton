// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/function_generator.h"

#include "proton/core/spice_number.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string>

namespace proton::instruments {

namespace {

using core::formatSpiceNumber;

double finiteOr(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

// `value` rounded to 12 significant digits, so arithmetic noise does not reach the netlist.
double rounded(double value)
{
    std::array<char, 64> buffer{};
    const auto end = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                   std::chars_format::scientific, 11)
                         .ptr;
    double result = value;
    (void)std::from_chars(buffer.data(), end, result);
    return result;
}

std::string number(double value)
{
    return formatSpiceNumber(rounded(value));
}

std::string sourceSpec(const FunctionGenerator::Config& config)
{
    using Waveform = FunctionGenerator::Waveform;
    const double low = config.offset - config.amplitude;
    const double high = config.offset + config.amplitude;
    const double period = 1.0 / config.frequency;
    const double edge = period * FunctionGenerator::kEdgeFraction;

    switch (config.waveform) {
    case Waveform::Sine:
        return "SIN(" + number(config.offset) + " " + number(config.amplitude) + " " +
               number(config.frequency) + ")";
    case Waveform::Square:
        // Rise, high, fall, low: the plateaus are equal, so the mean is the offset.
        return "PULSE(" + number(low) + " " + number(high) + " 0 " + number(edge) + " " +
               number(edge) + " " + number(period / 2 - edge) + " " + number(period) + ")";
    case Waveform::Triangle: {
        // ngspice reads a pulse width of 0 as "until the end of the run", so the triangle dwells
        // for one edge time at each peak instead of turning sharply.
        const double ramp = period / 2 - edge;
        return "PULSE(" + number(low) + " " + number(high) + " 0 " + number(ramp) + " " +
               number(ramp) + " " + number(edge) + " " + number(period) + ")";
    }
    case Waveform::Dc:
        break;
    }
    return "DC " + number(config.offset);
}

} // namespace

FunctionGenerator::Config FunctionGenerator::sanitise(const Config& config)
{
    const Config defaults;
    Config result;
    switch (config.waveform) {
    case Waveform::Sine:
    case Waveform::Square:
    case Waveform::Triangle:
    case Waveform::Dc:
        result.waveform = config.waveform;
        break;
    }
    result.frequency =
        std::clamp(finiteOr(config.frequency, defaults.frequency), kMinFrequency, kMaxFrequency);
    result.amplitude = std::clamp(finiteOr(config.amplitude, defaults.amplitude), 0.0, kMaxVoltage);
    const double maxOffset =
        result.waveform == Waveform::Dc ? kMaxVoltage : kMaxVoltage - result.amplitude;
    result.offset = std::clamp(finiteOr(config.offset, defaults.offset), -maxOffset, maxOffset);
    result.outputImpedance = std::clamp(finiteOr(config.outputImpedance, defaults.outputImpedance),
                                        0.0, kMaxOutputImpedance);
    result.output = config.output;
    return result;
}

SpiceFragment FunctionGenerator::toSpice(std::string_view name, std::string_view outputNet,
                                         std::string_view referenceNet) const
{
    const std::string instance(name);
    const std::string reference(referenceNet);
    const std::string spec = sourceSpec(config_);

    SpiceFragment fragment;
    if (!config_.output) {
        const std::string internal = instance + "_int";
        fragment.lines.push_back("* function generator " + instance + ", output off");
        fragment.lines.push_back("V" + instance + " " + internal + " " + reference + " DC 0");
        fragment.lines.push_back("R" + instance + " " + internal + " " +
                                 std::string(outputNet) + " " + number(kOffResistance));
        return fragment;
    }
    fragment.lines.push_back("* function generator " + instance);
    if (config_.outputImpedance == 0.0) {
        fragment.lines.push_back("V" + instance + " " + std::string(outputNet) + " " + reference +
                                 " " + spec);
        return fragment;
    }
    const std::string internal = instance + "_int";
    fragment.lines.push_back("V" + instance + " " + internal + " " + reference + " " + spec);
    fragment.lines.push_back("R" + instance + " " + internal + " " + std::string(outputNet) + " " +
                             number(config_.outputImpedance));
    return fragment;
}

} // namespace proton::instruments
