// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/selector.h"

#include <algorithm>
#include <cmath>

namespace proton::instruments {

Selector::Selector(std::size_t positions, double span, std::size_t initial)
    : positions_(std::max<std::size_t>(positions, 1)),
      span_(std::isfinite(span) ? std::clamp(span, 0.0, 360.0) : 0.0)
{
    setPosition(initial);
}

bool Selector::setPosition(std::size_t position)
{
    const std::size_t clamped = std::min(position, positions_ - 1);
    const bool moved = clamped != position_;
    position_ = clamped;
    return moved;
}

bool Selector::turn(int clicks)
{
    const long long target = static_cast<long long>(position_) + clicks;
    const long long last = static_cast<long long>(positions_) - 1;
    return setPosition(static_cast<std::size_t>(std::clamp(target, 0LL, last)));
}

bool Selector::drag(double pixels)
{
    if (!std::isfinite(pixels)) {
        return false;
    }
    dragRemainder_ += pixels / kPixelsPerClick;
    const double clicks = std::trunc(dragRemainder_);
    dragRemainder_ -= clicks;
    return clicks != 0.0 && turn(static_cast<int>(clicks));
}

double Selector::angleOf(std::size_t position) const
{
    if (positions_ < 2) {
        return 0.0;
    }
    const double step = span_ / static_cast<double>(positions_ - 1);
    return -span_ / 2.0 + step * static_cast<double>(std::min(position, positions_ - 1));
}

} // namespace proton::instruments
