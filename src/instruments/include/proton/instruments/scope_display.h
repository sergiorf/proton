// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/instruments/scope.h"

#include <cstddef>
#include <span>
#include <vector>

namespace proton::instruments {

// How a ScopeFrame lands on the screen, in divisions, for a display to draw (ADR 0004). The screen
// is kHorizontalDivisions wide and kVerticalDivisions high: x runs from -5 (left edge) to +5
// (right edge) with the trigger at 0, y from -4 (bottom) to +4 (top) with each channel's offset at
// 0. Qt-free, so the geometry is tested here and the widget only scales it to pixels.

// Screen extent in divisions from the centre: 5 across, 4 up and down.
inline constexpr double kHalfWidthDivisions = ScopeFrontEnd::kHorizontalDivisions / 2.0;
inline constexpr double kHalfHeightDivisions = ScopeFrontEnd::kVerticalDivisions / 2.0;

// A voltage of `channel` in divisions from the centre of the screen: (volts - offset) / V/div.
double toDivisions(double volts, const Scope::ChannelConfig& channel);

// Where sample `index` of `frame` is, in divisions from the trigger: time / (time/div).
double sampleX(const ScopeFrame& frame, std::size_t index, double timePerDiv);

// The trigger level as a height on the screen: toDivisions() of the level on the source channel.
// Off the screen when the level is beyond it; the display clamps the marker to the edge.
double triggerLevelY(const Scope::Config& config);

// One column of a decimated trace: the lowest and highest value it covers.
struct TraceColumn
{
    double min = 0.0;
    double max = 0.0;

    friend bool operator==(const TraceColumn&, const TraceColumn&) = default;
};

// `values` decimated to `columns` columns of equal width for drawing at one column per pixel
// (min/max decimation, ADR 0004): column c covers samples [c n / columns, (c + 1) n / columns) of
// the n values, and its range also takes in the last sample of the column before, so that drawing
// each column as a vertical line gives an unbroken trace whose peaks are never lost. A column that
// holds no sample (more columns than samples) repeats the last sample before it. Empty when
// `values` is empty or `columns` is not positive.
std::vector<TraceColumn> decimate(std::span<const double> values, int columns);

} // namespace proton::instruments
