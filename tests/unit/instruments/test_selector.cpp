// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/selector.h"
#include "proton/test/test.h"

using proton::instruments::Selector;

TEST(a_selector_turns_one_position_per_click_between_its_stops)
{
    Selector dial(4, 90.0);
    CHECK_EQ(dial.position(), 0u);
    CHECK(dial.turn(2));
    CHECK_EQ(dial.position(), 2u);
    CHECK(dial.turn(5));
    CHECK_EQ(dial.position(), 3u);
    CHECK(!dial.turn(1)); // at the stop
    CHECK(dial.turn(-10));
    CHECK_EQ(dial.position(), 0u);
}

TEST(positions_spread_evenly_over_the_span_centred_up)
{
    Selector dial(3, 90.0, 1);
    CHECK_EQ(dial.angleOf(0), -45.0);
    CHECK_EQ(dial.angle(), 0.0);
    CHECK_EQ(dial.angleOf(2), 45.0);
    CHECK_EQ(Selector(1, 90.0).angle(), 0.0);
}

TEST(a_drag_turns_one_position_per_few_pixels)
{
    Selector dial(3, 60.0);
    CHECK(!dial.drag(Selector::kPixelsPerClick * 0.5));
    CHECK(dial.drag(Selector::kPixelsPerClick * 0.6));
    CHECK_EQ(dial.position(), 1u);
    dial.endDrag();
    CHECK(!dial.drag(Selector::kPixelsPerClick * 0.9));
}

TEST(set_position_clamps)
{
    Selector dial(2, 60.0);
    CHECK(dial.setPosition(7));
    CHECK_EQ(dial.position(), 1u);
    CHECK(!dial.setPosition(1));
}
