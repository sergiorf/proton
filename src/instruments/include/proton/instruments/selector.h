// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>

namespace proton::instruments {

// The logic of a rotary selector switch, such as a multimeter's mode dial (PLAN.md M2.5), Qt-free
// so it is tested here and the widget only draws it. Unlike a Knob it has end stops and a fixed
// angle per position: the positions are spread evenly over `span` degrees, centred on straight up.
//
// Input arrives as clicks, one position each: a wheel notch or arrow key, or a vertical drag of
// kPixelsPerClick (upward turns clockwise, to the next position). A drag keeps the fraction of a
// click it has not used until it ends. Turning past an end stop does nothing.
class Selector
{
public:
    static constexpr double kPixelsPerClick = 16.0;

    // A switch with `positions` positions (at least one) over `span` degrees, at `initial`.
    Selector(std::size_t positions, double span, std::size_t initial = 0);

    std::size_t positions() const { return positions_; }
    std::size_t position() const { return position_; }
    // Moves to `position`, clamped to the last one. Returns true if it moved.
    bool setPosition(std::size_t position);

    // Turns by `clicks` positions, stopping at the ends. Returns true if it moved.
    bool turn(int clicks);
    // A vertical drag of `pixels` (upward positive) since the last call.
    bool drag(double pixels);
    void endDrag() { dragRemainder_ = 0.0; }

    // The pointer's angle at `position`, degrees clockwise from straight up.
    double angleOf(std::size_t position) const;
    double angle() const { return angleOf(position_); }

private:
    std::size_t positions_;
    double span_;
    std::size_t position_ = 0;
    double dragRemainder_ = 0.0;
};

} // namespace proton::instruments
