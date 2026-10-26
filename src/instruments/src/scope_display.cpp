// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/scope_display.h"

#include <algorithm>

namespace proton::instruments {

double toDivisions(double volts, const Scope::ChannelConfig& channel)
{
    return (volts - channel.offset) / channel.voltsPerDiv;
}

double sampleX(const ScopeFrame& frame, std::size_t index, double timePerDiv)
{
    return frame.triggerX +
           (frame.startTime + static_cast<double>(index) * frame.sampleInterval) / timePerDiv;
}

std::vector<std::size_t> tracesToDraw(const Scope::Config& config)
{
    std::vector<std::size_t> channels;
    for (std::size_t c = Scope::kChannels; c-- > 0;) {
        if (config.channels[c].shown) {
            channels.push_back(c);
        }
    }
    return channels;
}

double triggerLevelY(const Scope::Config& config)
{
    const std::size_t source = config.trigger.source == Scope::TriggerSource::Channel2 ? 1 : 0;
    return toDivisions(config.trigger.level, config.channels[source]);
}

std::vector<TraceColumn> decimate(std::span<const double> values, int columns)
{
    std::vector<TraceColumn> result;
    if (values.empty() || columns <= 0) {
        return result;
    }
    const std::size_t n = values.size();
    const auto count = static_cast<std::size_t>(columns);
    result.reserve(count);
    double last = values.front(); // the last sample before the column
    for (std::size_t c = 0; c < count; ++c) {
        const std::size_t begin = c * n / count;
        const std::size_t end = (c + 1) * n / count;
        TraceColumn column{last, last};
        for (std::size_t i = begin; i < end; ++i) {
            column.min = std::min(column.min, values[i]);
            column.max = std::max(column.max, values[i]);
        }
        if (end > begin) {
            last = values[end - 1];
        }
        result.push_back(column);
    }
    return result;
}

} // namespace proton::instruments
