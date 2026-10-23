// SPDX-License-Identifier: AGPL-3.0-only
// RealTimePacer (M1.7, decision D6): which simulation time is due at a given wall-clock time.
// Wall-clock time is passed in, so these tests are exact.
#include "proton/sim/real_time_pacer.h"
#include "proton/test/test.h"

using proton::sim::RealTimePacer;

TEST(simulation_time_is_due_as_wall_clock_time_passes)
{
    RealTimePacer pacer;
    pacer.start(10.0);
    CHECK_EQ(pacer.due(10.0), 0.0);
    CHECK_EQ(pacer.due(10.25), 0.25);
    CHECK_EQ(pacer.due(12.0), 2.0);
}

TEST(pacing_can_start_at_any_simulation_time)
{
    RealTimePacer pacer;
    pacer.start(5.0, 2.0);
    CHECK_EQ(pacer.due(5.0), 2.0);
    CHECK_EQ(pacer.due(6.0), 3.0);
}

TEST(nothing_is_due_before_the_start)
{
    RealTimePacer pacer;
    pacer.start(5.0, 2.0);
    CHECK_EQ(pacer.due(4.0), 2.0);
}

TEST(a_lag_within_the_limit_is_made_up)
{
    RealTimePacer pacer;
    pacer.start(0.0);
    pacer.fed(1.0, 1.0 - RealTimePacer::kMaxLag);
    CHECK_EQ(pacer.due(1.0), 1.0);
}

// A simulator that fell behind does not race to catch up afterwards: what is due slips so that
// the simulation is at most kMaxLag behind it.
TEST(a_longer_lag_slips_what_is_due)
{
    RealTimePacer pacer;
    pacer.start(0.0);
    pacer.fed(1.0, 0.5);
    CHECK_NEAR(pacer.due(1.0), 0.5 + RealTimePacer::kMaxLag, 1e-12);
    CHECK_NEAR(pacer.due(1.2), 0.5 + RealTimePacer::kMaxLag + 0.2, 1e-12);
}

TEST(speed_is_one_until_the_first_window_ends)
{
    RealTimePacer pacer;
    pacer.start(0.0);
    pacer.fed(0.25, 0.05);
    CHECK_EQ(pacer.speed(), 1.0);
}

TEST(speed_is_one_while_the_simulator_keeps_up)
{
    RealTimePacer pacer;
    pacer.start(0.0);
    pacer.fed(0.25, 0.25);
    pacer.fed(0.5, 0.5);
    CHECK_EQ(pacer.speed(), 1.0);
    pacer.fed(1.0, 1.0);
    CHECK_EQ(pacer.speed(), 1.0);
}

TEST(speed_is_the_fraction_of_real_time_when_the_simulator_falls_behind)
{
    RealTimePacer pacer;
    pacer.start(0.0);
    pacer.fed(0.5, 0.2);
    CHECK_NEAR(pacer.speed(), 0.4, 1e-12);
    pacer.fed(1.0, 0.45);
    CHECK_NEAR(pacer.speed(), 0.5, 1e-12);
}

TEST(start_begins_again)
{
    RealTimePacer pacer;
    pacer.start(0.0);
    pacer.fed(0.5, 0.2);
    pacer.start(3.0);
    CHECK_EQ(pacer.due(3.5), 0.5);
    CHECK_EQ(pacer.speed(), 1.0);
}
