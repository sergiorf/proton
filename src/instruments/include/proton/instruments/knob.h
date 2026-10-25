// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <vector>

namespace proton::instruments {

// The logic of a front-panel rotary knob (PLAN.md M2.4), Qt-free so it is tested here and the
// widget only draws it and forwards input. Knobs are endless rotary encoders, as on a digital bench
// instrument: they turn in clicks and have no end stops, but the value they set does.
//
// - A stepped knob moves through a fixed list of values, one click per step, in the order a
//   clockwise turn goes: time/div and V/div on the 1-2-5 sequence, decreasing, since turning them
//   clockwise makes the trace larger as on a real scope. setValue() snaps to the nearest step.
// - A continuous knob moves by `step` per click between `min` and `max`: position, trigger level.
//   Turned fast, it accelerates (turnAccelerated), so a long way takes few turns and a fine
//   adjustment stays fine. Pushing it sets its reset value, as pushing a real position knob
//   centres the trace.
//
// Input arrives as clicks: one per mouse-wheel notch or arrow key, and one per kPixelsPerClick of a
// vertical drag (upward turns up). A drag keeps the fraction of a click it has not used until the
// drag ends. Clicks that would take the value past its range are not applied, so the pointer only
// turns when the value changes, and angle() gives where the pointer points.
class Knob
{
public:
    static constexpr double kPixelsPerClick = 8.0;
    static constexpr double kDegreesPerClick = 18.0; // 20 clicks per turn
    // Acceleration of a continuous knob: clicks closer together than these get this many steps.
    static constexpr double kFastInterval = 0.04; // s between clicks
    static constexpr double kFastFactor = 10.0;
    static constexpr double kQuickInterval = 0.12;
    static constexpr double kQuickFactor = 4.0;

    // A knob through `steps`, in clockwise order, at the step nearest `initial`. With no steps its
    // value is always 0.
    static Knob stepped(std::vector<double> steps, double initial);
    // A knob from `min` to `max` in steps of `step`, at `initial` (clamped); pushing it sets `reset`.
    static Knob continuous(double min, double max, double step, double initial, double reset);

    bool isStepped() const { return stepped_; }
    double value() const;
    // Sets the value without turning the pointer: snapped to the nearest step, or clamped.
    void setValue(double value);

    // Continuous knobs: the amount one click changes the value, and the push value. Stepped knobs
    // ignore them.
    void setStep(double step);
    double step() const { return step_; }
    void setReset(double reset) { reset_ = reset; }

    // Turns by `clicks` (negative turns down). Returns true if the value changed.
    bool turn(int clicks);
    // As turn(), accelerated for a continuous knob when the previous click came less than
    // kQuickInterval or kFastInterval seconds of wall-clock time before.
    bool turnAccelerated(int clicks, double secondsSinceLastClick);
    // A vertical drag of `pixels` (upward positive) since the last call. Returns true if the value
    // changed.
    bool drag(double pixels);
    void endDrag() { dragRemainder_ = 0.0; }
    // Pushes the knob: a continuous knob goes to its reset value. Returns true if the value changed.
    bool push();

    // Where the pointer points, degrees clockwise from straight up, in [0, 360).
    double angle() const;

private:
    Knob() = default;
    bool apply(double clicks);

    bool stepped_ = false;
    std::vector<double> steps_;
    std::size_t index_ = 0; // stepped
    double value_ = 0.0;    // continuous
    double min_ = 0.0;
    double max_ = 0.0;
    double step_ = 1.0;
    double reset_ = 0.0;
    double turned_ = 0.0; // clicks applied, for the pointer
    double dragRemainder_ = 0.0;
};

} // namespace proton::instruments
