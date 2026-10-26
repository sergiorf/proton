// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/knob.h"
#include "proton/instruments/knob_steps.h"
#include "proton/test/test.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using proton::instruments::Knob;

namespace {

Knob timebase()
{
    return Knob::stepped(proton::instruments::oneTwoFiveSteps(1e-9, 10.0), 200e-6);
}

Knob position()
{
    return Knob::continuous(-10.0, 10.0, 0.1, 0.0, 0.0);
}

} // namespace

TEST(a_stepped_knob_moves_one_step_per_click)
{
    Knob knob = timebase();
    CHECK_EQ(knob.value(), 200e-6);
    CHECK(knob.turn(1));
    CHECK_EQ(knob.value(), 500e-6);
    CHECK(knob.turn(-3));
    CHECK_EQ(knob.value(), 50e-6);
}

TEST(a_stepped_knob_stops_at_its_ends_and_reports_no_change)
{
    Knob knob = Knob::stepped({1.0, 2.0, 5.0}, 5.0);
    CHECK(!knob.turn(1));
    CHECK_EQ(knob.value(), 5.0);
    CHECK(knob.turn(-10));
    CHECK_EQ(knob.value(), 1.0);
    CHECK(!knob.turn(-1));
}

TEST(a_stepped_knob_snaps_to_the_nearest_step)
{
    Knob knob = timebase();
    knob.setValue(0.3e-3);
    CHECK_EQ(knob.value(), 0.2e-3);
    knob.setValue(0.4e-3);
    CHECK_EQ(knob.value(), 0.5e-3);
}

TEST(a_stepped_knob_does_not_accelerate_or_reset)
{
    Knob knob = timebase();
    CHECK(knob.turnAccelerated(1, 0.001));
    CHECK_EQ(knob.value(), 500e-6);
    CHECK(!knob.push());
    CHECK_EQ(knob.value(), 500e-6);
}

TEST(a_continuous_knob_moves_by_its_step_within_its_range)
{
    Knob knob = position();
    CHECK(knob.turn(3));
    CHECK_NEAR(knob.value(), 0.3, 1e-12);
    knob.setValue(9.95);
    CHECK(knob.turn(1));
    CHECK_EQ(knob.value(), 10.0);
    CHECK(!knob.turn(1));
    knob.setValue(-50.0);
    CHECK_EQ(knob.value(), -10.0);
}

TEST(a_continuous_knob_accelerates_when_turned_fast)
{
    Knob knob = position();
    CHECK(knob.turnAccelerated(1, 1.0)); // slow: one step
    CHECK_NEAR(knob.value(), 0.1, 1e-12);
    CHECK(knob.turnAccelerated(1, 0.08)); // quick
    CHECK_NEAR(knob.value(), 0.1 + 0.1 * Knob::kQuickFactor, 1e-12);
    CHECK(knob.turnAccelerated(-1, 0.01)); // fast
    CHECK_NEAR(knob.value(), 0.1 + 0.1 * Knob::kQuickFactor - 0.1 * Knob::kFastFactor, 1e-12);
}

TEST(the_step_follows_the_scale)
{
    Knob knob = position();
    knob.setStep(0.02); // a tenth of a division at 200 mV/div
    CHECK(knob.turn(5));
    CHECK_NEAR(knob.value(), 0.1, 1e-12);
    knob.setStep(0.0); // ignored
    CHECK_EQ(knob.step(), 0.02);
}

TEST(pushing_a_continuous_knob_resets_it)
{
    Knob knob = Knob::continuous(-10.0, 10.0, 0.1, 2.5, 0.5);
    CHECK(knob.push());
    CHECK_EQ(knob.value(), 0.5);
    CHECK(!knob.push()); // already there
    knob.setReset(1.5);
    CHECK(knob.push());
    CHECK_EQ(knob.value(), 1.5);
}

TEST(a_drag_turns_one_click_per_few_pixels_and_keeps_the_fraction)
{
    Knob knob = timebase();
    CHECK(!knob.drag(Knob::kPixelsPerClick * 0.6));
    CHECK(knob.drag(Knob::kPixelsPerClick * 0.6)); // 1.2 clicks in all
    CHECK_EQ(knob.value(), 500e-6);
    CHECK(knob.drag(-Knob::kPixelsPerClick * 2.0)); // 0.2 - 2 = -1.8: one click down
    CHECK_EQ(knob.value(), 200e-6);
    knob.endDrag(); // the -0.8 left over is dropped
    CHECK(!knob.drag(-Knob::kPixelsPerClick * 0.5));
    CHECK_EQ(knob.value(), 200e-6);
}

TEST(a_continuous_knob_turns_its_pointer_only_with_its_value)
{
    Knob knob = position();
    knob.turn(2);
    CHECK_NEAR(knob.angle(), 2 * Knob::kDegreesPerClick, 1e-9);
    knob.setValue(10.0); // set, not turned
    CHECK_NEAR(knob.angle(), 2 * Knob::kDegreesPerClick, 1e-9);
    knob.turn(5); // at the end: no change, no turn
    CHECK_NEAR(knob.angle(), 2 * Knob::kDegreesPerClick, 1e-9);
    knob.turn(-3);
    CHECK_NEAR(knob.angle(), 360.0 - Knob::kDegreesPerClick, 1e-9); // endless
}

// A stepped knob has a printed scale (M4.5): its pointer points at the mark of its step, the
// marks spread over kScaleArc degrees centred on straight up.
TEST(a_stepped_knob_points_at_the_mark_of_its_step)
{
    Knob knob = Knob::stepped({1.0, 2.0, 5.0}, 1.0);
    CHECK_EQ(knob.stepCount(), 3u);
    CHECK_NEAR(knob.markAngle(0), 360.0 - Knob::kScaleArc / 2.0, 1e-9);
    CHECK_NEAR(knob.markAngle(1), 0.0, 1e-9);
    CHECK_NEAR(knob.markAngle(2), Knob::kScaleArc / 2.0, 1e-9);
    CHECK_NEAR(knob.angle(), knob.markAngle(0), 1e-9);
    knob.turn(1);
    CHECK_NEAR(knob.angle(), knob.markAngle(1), 1e-9);
    knob.setValue(5.0); // a set value moves the pointer to its mark too
    CHECK_NEAR(knob.angle(), knob.markAngle(2), 1e-9);
    CHECK_EQ(Knob::stepped({7.0}, 7.0).markAngle(0), 0.0);
}

TEST(fine_and_coarse_turn_a_continuous_knob_a_tenth_and_ten_clicks)
{
    Knob knob = position(); // 0.1 a click
    CHECK(knob.turn(1, Knob::Speed::Fine));
    CHECK_NEAR(knob.value(), 0.01, 1e-12);
    CHECK(knob.turn(1, Knob::Speed::Coarse));
    CHECK_NEAR(knob.value(), 1.01, 1e-12);
    CHECK(knob.turn(-2, Knob::Speed::Fine));
    CHECK_NEAR(knob.value(), 0.99, 1e-12);
    // Fine and coarse never accelerate: the speed is the student's choice.
    CHECK(knob.turnAccelerated(1, 0.001, Knob::Speed::Fine));
    CHECK_NEAR(knob.value(), 1.0, 1e-12);
    // A drag is fine or coarse the same way.
    CHECK(knob.drag(Knob::kPixelsPerClick, Knob::Speed::Coarse));
    CHECK_NEAR(knob.value(), 2.0, 1e-12);
    CHECK(knob.drag(Knob::kPixelsPerClick, Knob::Speed::Fine));
    CHECK_NEAR(knob.value(), 2.01, 1e-12);
}

TEST(a_stepped_knob_turns_one_step_a_click_at_any_speed)
{
    Knob knob = timebase();
    CHECK(knob.turn(1, Knob::Speed::Fine));
    CHECK_EQ(knob.value(), 500e-6);
    CHECK(knob.turn(1, Knob::Speed::Coarse));
    CHECK_EQ(knob.value(), 1e-3);
}

// The fine variable V/div of a bench scope: pushing the scale knob lets it move between the 1-2-5
// steps; pushing it again puts it back on the nearest step.
TEST(a_variable_knob_moves_between_its_steps_and_snaps_back_when_pushed)
{
    std::vector<double> steps = proton::instruments::oneTwoFiveSteps(1e-3, 10.0);
    std::reverse(steps.begin(), steps.end()); // clockwise: a smaller V/div
    Knob knob = Knob::stepped(steps, 1.0);
    knob.setVariableAllowed(true);
    CHECK(!knob.variable());
    CHECK(!knob.push()); // the value stays; the knob is now variable
    CHECK(knob.variable());
    CHECK(knob.turn(1));
    CHECK_NEAR(knob.value(), 1.0 / Knob::kVariableRatio, 1e-12);
    for (int i = 0; i < 5; ++i) {
        knob.turn(1);
    }
    CHECK(knob.value() < 1.0 && knob.value() > 0.5); // between two steps
    // Its pointer is between the marks of 1 V and 500 mV, steps 3 and 4.
    CHECK(knob.angle() > knob.markAngle(3) && knob.angle() < knob.markAngle(4));
    knob.setValue(0.7); // a variable knob keeps any value in its range
    CHECK_EQ(knob.value(), 0.7);
    CHECK(knob.push()); // back on the nearest step
    CHECK(!knob.variable());
    CHECK_EQ(knob.value(), 0.5);
    CHECK(knob.turn(1)); // and by steps again
    CHECK_EQ(knob.value(), 0.2);
}

TEST(a_knob_without_a_variable_mode_ignores_a_push)
{
    Knob knob = timebase();
    CHECK(!knob.push());
    CHECK(!knob.variable());
    CHECK(knob.turn(1));
    CHECK_EQ(knob.value(), 500e-6);
}

// Turning the mouse around the knob's centre, as a hand turns a knob.
TEST(a_full_circular_turn_is_twenty_clicks)
{
    Knob knob = Knob::continuous(-100.0, 100.0, 1.0, 0.0, 0.0);
    for (int i = 0; i < 36; ++i) {
        knob.rotate(10.0);
    }
    CHECK_NEAR(knob.value(), 360.0 / Knob::kDegreesPerClick, 1e-12);
    CHECK_NEAR(knob.value(), 20.0, 1e-12);
    CHECK(knob.rotate(-360.0));
    CHECK_NEAR(knob.value(), 0.0, 1e-12);
    // Less than a click is kept until the next move, as a drag keeps it.
    CHECK(!knob.rotate(Knob::kDegreesPerClick * 0.6));
    CHECK(knob.rotate(Knob::kDegreesPerClick * 0.6));
    CHECK_NEAR(knob.value(), 1.0, 1e-12);
}

TEST(a_stepped_knob_can_turn_down_its_values_clockwise)
{
    std::vector<double> steps = proton::instruments::oneTwoFiveSteps(1e-3, 10.0);
    std::reverse(steps.begin(), steps.end());
    Knob knob = Knob::stepped(steps, 1.0);
    CHECK(knob.turn(1)); // clockwise: a smaller V/div, a larger trace
    CHECK_EQ(knob.value(), 0.5);
    CHECK(knob.turn(-2));
    CHECK_EQ(knob.value(), 2.0);
}

// Dragging the mouse on a knob (dragBetween): the hand's path, point by point, in screen pixels
// with y downwards, as the widget passes it. These are the gestures a student makes; a turn of the
// hand clockwise, across the top and down the right side, must turn the knob clockwise all the way
// (a straight up/right rule turned it back on the way down).
namespace {

using Point = Knob::Point;
constexpr Point kCentre{100.0, 100.0};
constexpr double kRadius = 25.0; // the hand on the knob's edge

Point onCircle(double degrees, double radius = kRadius)
{
    const double a = degrees * std::numbers::pi / 180.0;
    return {kCentre.x + radius * std::sin(a), kCentre.y - radius * std::cos(a)};
}

// The hand going round from `fromDegrees` to `toDegrees` (clockwise from straight up) in `steps`
// moves; returns whether any move changed the value.
bool turnHand(Knob& knob, double fromDegrees, double toDegrees, Knob::DragMode mode,
              int steps = 18)
{
    bool changed = false;
    Point last = onCircle(fromDegrees);
    for (int i = 1; i <= steps; ++i) {
        const Point next = onCircle(fromDegrees + (toDegrees - fromDegrees) * i / steps);
        changed = knob.dragBetween(last, next, kCentre, mode) || changed;
        last = next;
    }
    knob.endDrag();
    return changed;
}

// A straight drag from `from` by (dx, dy) in `steps` moves.
void dragHand(Knob& knob, Point from, double dx, double dy, Knob::DragMode mode, int steps = 4)
{
    Point last = from;
    for (int i = 1; i <= steps; ++i) {
        const Point next{from.x + dx * i / steps, from.y + dy * i / steps};
        knob.dragBetween(last, next, kCentre, mode);
        last = next;
    }
    knob.endDrag();
}

// How far `to` is clockwise from `from`, in (-180, 180]: positive is clockwise, through straight
// up too (350 to 40 degrees is 50 clockwise).
double clockwise(double from, double to)
{
    double d = std::fmod(to - from + 540.0, 360.0) - 180.0;
    return d == -180.0 ? 180.0 : d;
}

Knob counter()
{
    return Knob::continuous(-100.0, 100.0, 1.0, 0.0, 0.0); // one click is 1
}

} // namespace

TEST(a_clockwise_turn_of_the_hand_turns_the_knob_clockwise_all_the_way)
{
    Knob knob = counter();
    CHECK(turnHand(knob, 0.0, 180.0, Knob::DragMode::Circular)); // top, right side, bottom
    CHECK_NEAR(knob.value(), 10.0, 1e-9);                       // half a turn is ten clicks
    // Each part of the way turns it on, never back: the way down the right side too.
    Knob part = counter();
    turnHand(part, 0.0, 90.0, Knob::DragMode::Circular);
    const double atRight = part.value();
    turnHand(part, 90.0, 180.0, Knob::DragMode::Circular);
    CHECK(atRight > 0.0);
    CHECK(part.value() > atRight);
}

TEST(an_anticlockwise_turn_of_the_hand_turns_the_knob_anticlockwise)
{
    Knob knob = counter();
    CHECK(turnHand(knob, 0.0, -180.0, Knob::DragMode::Circular)); // top, left side, bottom
    CHECK_NEAR(knob.value(), -10.0, 1e-9);
}

TEST(a_turn_of_the_hand_through_the_bottom_does_not_jump)
{
    // From the right side, down through the bottom, where the angle wraps from 180 to -180.
    Knob knob = counter();
    turnHand(knob, 90.0, 270.0, Knob::DragMode::Circular);
    CHECK_NEAR(knob.value(), 10.0, 1e-9);
    // A whole turn and back again, in many small moves.
    Knob whole = counter();
    turnHand(whole, 0.0, 360.0, Knob::DragMode::Circular, 72);
    CHECK_NEAR(whole.value(), 20.0, 1e-9);
    turnHand(whole, 360.0, 0.0, Knob::DragMode::Circular, 72);
    CHECK_NEAR(whole.value(), 0.0, 1e-9);
}

TEST(a_turn_counts_the_angle_not_the_distance)
{
    // The same quarter turn near the cap's edge and far outside it is the same five clicks.
    for (const double radius : {12.0, 25.0, 80.0}) {
        Knob knob = counter();
        Point last = onCircle(0.0, radius);
        for (int i = 1; i <= 9; ++i) {
            const Point next = onCircle(10.0 * i, radius);
            knob.dragBetween(last, next, kCentre, Knob::DragMode::Circular);
            last = next;
        }
        CHECK_NEAR(knob.value(), 5.0, 1e-9);
    }
}

TEST(near_the_centre_a_round_drag_turns_as_a_straight_one)
{
    // Within kDeadZone of the centre the angle means nothing: a move from there turns as a
    // straight drag does, up or right clockwise.
    Knob up = counter();
    for (int i = 0; i < 4; ++i) { // moves to the right, each from just below the centre
        const Point from{kCentre.x, kCentre.y + 2.0};
        up.dragBetween(from, {from.x + Knob::kPixelsPerClick, from.y}, kCentre,
                       Knob::DragMode::Circular);
    }
    CHECK_NEAR(up.value(), 4.0, 1e-9);
}

TEST(a_straight_drag_turns_with_the_hand_whatever_it_drifts)
{
    const double pixels = 4 * Knob::kPixelsPerClick;
    const Point start{kCentre.x, kCentre.y - kRadius};
    Knob right = counter();
    dragHand(right, start, pixels, pixels / 4, Knob::DragMode::Straight); // right, drifting down
    CHECK(right.value() > 0.0);
    Knob left = counter();
    dragHand(left, start, -pixels, -pixels / 4, Knob::DragMode::Straight); // left, drifting up
    CHECK(left.value() < 0.0);
    Knob up = counter();
    dragHand(up, start, 0.0, -pixels, Knob::DragMode::Straight);
    CHECK_NEAR(up.value(), 4.0, 1e-9);
    Knob down = counter();
    dragHand(down, start, 0.0, pixels, Knob::DragMode::Straight);
    CHECK_NEAR(down.value(), -4.0, 1e-9);
}

TEST(a_clockwise_turn_turns_a_stepped_knob_and_its_pointer_clockwise)
{
    // Time/div: clockwise is a faster timebase, and the pointer goes round the scale clockwise.
    std::vector<double> steps = proton::instruments::oneTwoFiveSteps(1e-9, 10.0);
    std::reverse(steps.begin(), steps.end());
    Knob knob = Knob::stepped(steps, 200e-6);
    const double angle = knob.angle();
    CHECK(turnHand(knob, 0.0, 90.0, Knob::DragMode::Circular));
    CHECK_EQ(knob.value(), 5e-6); // a quarter turn, five steps faster: 100u 50u 20u 10u 5u
    CHECK(clockwise(angle, knob.angle()) > 0.0); // here through straight up: 350 to 40 degrees
    Knob back = Knob::stepped(steps, 200e-6);
    CHECK(turnHand(back, 0.0, -90.0, Knob::DragMode::Circular));
    CHECK(back.value() > 200e-6);
    CHECK(clockwise(angle, back.angle()) < 0.0);
}

TEST(a_turn_of_the_hand_is_fine_or_coarse_like_any_turn)
{
    Knob fine = counter();
    Point last = onCircle(0.0);
    for (int i = 1; i <= 18; ++i) {
        const Point next = onCircle(10.0 * i);
        fine.dragBetween(last, next, kCentre, Knob::DragMode::Circular, Knob::Speed::Fine);
        last = next;
    }
    CHECK_NEAR(fine.value(), 1.0, 1e-9); // ten clicks at a tenth each
}
