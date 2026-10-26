// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <vector>

namespace proton::instruments {

// The logic of a front-panel rotary knob (PLAN.md M2.4, M4.5), Qt-free so it is tested here and the
// widget only draws it and forwards input.
//
// - A stepped knob moves through a fixed list of values, one click per step, in the order a
//   clockwise turn goes: time/div and V/div on the 1-2-5 sequence, decreasing, since turning them
//   clockwise makes the trace larger as on a real scope. setValue() snaps to the nearest step. It
//   has a printed scale: a mark per step spread over kScaleArc degrees centred on straight up,
//   its pointer at the mark of its step, and end stops at the first and last.
// - A stepped knob may have a variable mode (setVariableAllowed), as a bench scope's V/div does:
//   pushing the knob lets it move between its steps, a click multiplying or dividing the value by
//   kVariableRatio (30 clicks a decade) in the direction the steps go, its pointer between the
//   marks; pushing it again puts it on the nearest step.
// - A continuous knob moves by `step` per click between `min` and `max`: position, trigger level.
//   It is an endless rotary encoder, as on a digital instrument: no scale, its pointer turning a
//   click's worth with every change. Turned fast, it accelerates (turnAccelerated), so a long way
//   takes few turns and a fine adjustment stays fine. Pushing it sets its reset value, as pushing
//   a real position knob centres the trace.
//
// Input arrives as clicks: one per mouse-wheel notch or arrow key, one per kPixelsPerClick of a
// straight drag (up or right turns up), and one per kDegreesPerClick of a circular drag around the
// knob's centre (clockwise turns up). A drag keeps the fraction of a click it has not used until it
// ends. Every input has a speed: Fine turns a continuous (or variable) knob a tenth of a click,
// Coarse ten clicks; a stepped knob moves a step a click at any speed. Clicks that would take the
// value past its range are not applied, so the pointer only turns when the value changes, and
// angle() gives where the pointer points.
class Knob
{
public:
    enum class Speed {
        Normal,
        Fine,   // Shift held
        Coarse, // Ctrl held
    };

    // How a mouse drag on the knob turns it (dragBetween).
    enum class DragMode {
        Circular, // round the knob's centre, as a hand turns a knob
        Straight, // up or right clockwise, down or left anticlockwise
    };

    // A point on the screen, in pixels, y downwards.
    struct Point
    {
        double x = 0.0;
        double y = 0.0;
    };

    static constexpr double kPixelsPerClick = 8.0;
    static constexpr double kDegreesPerClick = 18.0; // 20 clicks per turn
    static constexpr double kFineFactor = 0.1;
    static constexpr double kCoarseFactor = 10.0;
    // Acceleration of a continuous knob: clicks closer together than these get this many steps.
    static constexpr double kFastInterval = 0.04; // s between clicks
    static constexpr double kFastFactor = 10.0;
    static constexpr double kQuickInterval = 0.12;
    static constexpr double kQuickFactor = 4.0;
    // Pixels from the centre within which a circular drag has no direction to speak of and turns as
    // a straight one does.
    static constexpr double kDeadZone = 6.0;
    // A stepped knob's scale, degrees from its first mark to its last.
    static constexpr double kScaleArc = 300.0;
    // A variable knob's ratio per click: 10^(1/30), 30 clicks a decade.
    static constexpr double kVariableRatio = 1.0797751623277096;

    // A knob through `steps`, in clockwise order, at the step nearest `initial`. With no steps its
    // value is always 0.
    static Knob stepped(std::vector<double> steps, double initial);
    // A knob from `min` to `max` in steps of `step`, at `initial` (clamped); a push sets `reset`.
    static Knob continuous(double min, double max, double step, double initial, double reset);

    bool isStepped() const { return stepped_; }
    double value() const;
    // Sets the value without turning a continuous knob's pointer: snapped to the nearest step (a
    // variable knob keeps any value between its first and last step), or clamped.
    void setValue(double value);

    // Continuous knobs: the amount one click changes the value, and the push value. Stepped knobs
    // ignore them.
    void setStep(double step);
    double step() const { return step_; }
    void setReset(double reset) { reset_ = reset; }

    // Stepped knobs with positive steps: whether a push switches the variable mode on and off.
    void setVariableAllowed(bool allowed);
    bool variable() const { return variable_; }

    // Turns by `clicks` (negative turns down). Returns true if the value changed.
    bool turn(int clicks, Speed speed = Speed::Normal);
    // As turn(), accelerated for a continuous knob at Normal speed when the previous click came
    // less than kQuickInterval or kFastInterval seconds of wall-clock time before.
    bool turnAccelerated(int clicks, double secondsSinceLastClick, Speed speed = Speed::Normal);
    // A straight drag of `pixels` (up or right positive), or a circular one of `degrees` (clockwise
    // positive), since the last call. Returns true if the value changed.
    bool drag(double pixels, Speed speed = Speed::Normal);
    bool rotate(double degrees, Speed speed = Speed::Normal);
    // A mouse move from `from` to `to` while the knob, whose centre is at `centre`, is held. In the
    // circular mode it turns by the angle the move sweeps round the centre (rotate): clockwise
    // across the top and down the right side is clockwise all the way, as a hand turns a real knob,
    // and a turn through the bottom does not jump. Within kDeadZone of the centre, and in the
    // straight mode, it turns by the move's rightward plus upward pixels (drag), so a drag to the
    // right that drifts down still turns clockwise. Returns true if the value changed.
    bool dragBetween(Point from, Point to, Point centre, DragMode mode,
                     Speed speed = Speed::Normal);
    void endDrag() { dragRemainder_ = 0.0; }
    // Pushes the knob: a continuous knob goes to its reset value; a stepped knob that allows it
    // goes into or out of its variable mode, out of it onto the nearest step. Returns true if the
    // value changed.
    bool push();

    // Where the pointer points, degrees clockwise from straight up, in [0, 360).
    double angle() const;
    // A stepped knob's scale: its steps, and where the mark of step `index` is, as angle().
    std::size_t stepCount() const { return steps_.size(); }
    double stepValue(std::size_t index) const { return steps_.at(index); }
    double markAngle(std::size_t index) const;

private:
    Knob() = default;
    bool apply(double clicks, Speed speed);
    bool accumulate(double clicks, Speed speed);
    std::size_t nearestStep(double value) const;
    // A stepped knob's place on its scale, in steps from the first; fractional when variable.
    double scalePosition() const;

    bool stepped_ = false;
    std::vector<double> steps_;
    std::size_t index_ = 0; // stepped, not variable
    double value_ = 0.0;    // continuous, or stepped while variable
    double min_ = 0.0;
    double max_ = 0.0;
    double step_ = 1.0;
    double reset_ = 0.0;
    bool variableAllowed_ = false;
    bool variable_ = false;
    double turned_ = 0.0; // clicks applied to a continuous knob, for its pointer
    double dragRemainder_ = 0.0;
};

} // namespace proton::instruments
