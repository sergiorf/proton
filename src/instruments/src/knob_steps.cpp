// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/knob_steps.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace proton::instruments {

namespace {

constexpr std::array<double, 3> kMantissas = {1.0, 2.0, 5.0};

// Step `index` of the sequence: 0 is 1, 1 is 2, 2 is 5, 3 is 10, -1 is 0.5, ...
double step(int index)
{
    const int decade = index >= 0 ? index / 3 : -((-index + 2) / 3);
    const int position = index - 3 * decade;
    // Exact for whole decades: 10^-3 x 2 is written as 2 / 10^3.
    const double scale = std::pow(10.0, std::abs(decade));
    const double mantissa = kMantissas[static_cast<std::size_t>(position)];
    return decade >= 0 ? mantissa * scale : mantissa / scale;
}

// The index of the step nearest `value` in log scale.
int nearest(double value)
{
    int index = static_cast<int>(std::floor(std::log10(value))) * 3;
    int best = index;
    for (int i = index - 3; i <= index + 3; ++i) {
        if (std::fabs(std::log(step(i) / value)) < std::fabs(std::log(step(best) / value))) {
            best = i;
        }
    }
    return best;
}

} // namespace

std::vector<double> oneTwoFiveSteps(double min, double max)
{
    std::vector<double> result;
    if (!(min > 0.0) || !(max >= min) || !std::isfinite(max)) {
        return result;
    }
    // A step within a part in 10^9 of a bound counts as on it.
    constexpr double kSlack = 1e-9;
    for (int i = nearest(min) - 1; step(i) <= max * (1.0 + kSlack); ++i) {
        if (step(i) >= min * (1.0 - kSlack)) {
            result.push_back(step(i));
        }
    }
    return result;
}

double turnOneTwoFive(double value, int clicks, double min, double max)
{
    const std::vector<double> steps = oneTwoFiveSteps(min, max);
    if (steps.empty() || !(value > 0.0) || !std::isfinite(value)) {
        return steps.empty() ? value : steps.front();
    }
    const double nearestStep = step(nearest(value));
    std::size_t at = 0;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (std::fabs(std::log(steps[i] / nearestStep)) <
            std::fabs(std::log(steps[at] / nearestStep))) {
            at = i;
        }
    }
    const long long target = static_cast<long long>(at) + clicks;
    const long long last = static_cast<long long>(steps.size()) - 1;
    return steps[static_cast<std::size_t>(target < 0 ? 0 : target > last ? last : target)];
}

} // namespace proton::instruments
