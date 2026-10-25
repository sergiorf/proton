// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/knob.h"
#include "proton/instruments/knob_steps.h"
#include "proton/test/test.h"

#include <algorithm>
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

TEST(the_pointer_turns_only_with_the_value)
{
    Knob knob = Knob::stepped({1.0, 2.0, 5.0}, 1.0);
    CHECK_EQ(knob.angle(), 0.0);
    knob.turn(2);
    CHECK_EQ(knob.angle(), 2 * Knob::kDegreesPerClick);
    knob.turn(5); // at the end: no change, no turn
    CHECK_EQ(knob.angle(), 2 * Knob::kDegreesPerClick);
    knob.turn(-2);
    knob.turn(-1);
    CHECK_EQ(knob.angle(), 0.0);
    knob.setValue(5.0); // set, not turned
    CHECK_EQ(knob.angle(), 0.0);

    Knob endless = position();
    endless.turn(-1);
    CHECK_NEAR(endless.angle(), 360.0 - Knob::kDegreesPerClick, 1e-9);
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
