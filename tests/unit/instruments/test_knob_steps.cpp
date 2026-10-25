// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/knob_steps.h"
#include "proton/test/test.h"

#include <vector>

using proton::instruments::oneTwoFiveSteps;
using proton::instruments::turnOneTwoFive;

TEST(steps_follow_one_two_five_inclusive)
{
    CHECK(oneTwoFiveSteps(1e-3, 10.0) ==
          (std::vector<double>{1e-3, 2e-3, 5e-3, 1e-2, 2e-2, 5e-2, 0.1, 0.2, 0.5, 1, 2, 5, 10}));
    CHECK(oneTwoFiveSteps(1e-9, 2e-9) == (std::vector<double>{1e-9, 2e-9}));
    CHECK(oneTwoFiveSteps(3.0, 40.0) == (std::vector<double>{5, 10, 20}));
}

TEST(steps_are_empty_for_bad_bounds)
{
    CHECK(oneTwoFiveSteps(0.0, 1.0).empty());
    CHECK(oneTwoFiveSteps(2.0, 1.0).empty());
    CHECK(oneTwoFiveSteps(3.0, 4.0).empty());
}

TEST(turning_moves_by_clicks_and_stops_at_the_ends)
{
    CHECK_EQ(turnOneTwoFive(200e-6, 1, 1e-9, 10.0), 500e-6);
    CHECK_EQ(turnOneTwoFive(200e-6, -1, 1e-9, 10.0), 100e-6);
    CHECK_EQ(turnOneTwoFive(200e-6, 3, 1e-9, 10.0), 2e-3);
    CHECK_EQ(turnOneTwoFive(5.0, 5, 1e-3, 10.0), 10.0);
    CHECK_EQ(turnOneTwoFive(2e-3, -5, 1e-3, 10.0), 1e-3);
}

TEST(turning_starts_from_the_nearest_step)
{
    CHECK_EQ(turnOneTwoFive(0.3, 0, 1e-3, 10.0), 0.2); // 0.3 is nearer 0.2 than 0.5 in log scale
    CHECK_EQ(turnOneTwoFive(0.4, 0, 1e-3, 10.0), 0.5);
    CHECK_EQ(turnOneTwoFive(0.3, 1, 1e-3, 10.0), 0.5);
}
