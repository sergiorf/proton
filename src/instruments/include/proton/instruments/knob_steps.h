// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <vector>

namespace proton::instruments {

// The 1-2-5 sequence of a bench instrument's stepped knobs (time/div, V/div): ..., 0.1, 0.2, 0.5,
// 1, 2, 5, 10, ...

// The steps of the sequence from `min` to `max`, both included when they are steps, in increasing
// order. Empty unless 0 < min <= max.
std::vector<double> oneTwoFiveSteps(double min, double max);

// The step `clicks` steps from the step nearest `value` (in log scale), clamped to [min, max]: what
// turning the knob `clicks` clicks does (negative turns it down). `value` itself if there are no
// steps between min and max.
double turnOneTwoFive(double value, int clicks, double min, double max);

} // namespace proton::instruments
