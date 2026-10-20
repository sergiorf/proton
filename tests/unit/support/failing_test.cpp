// SPDX-License-Identifier: AGPL-3.0-only
// Deliberately failing tests. check_failing.cmake runs this executable and expects a non-zero
// exit code and failure reports with file, line and both values.
#include "proton/test/test.h"

#include <stdexcept>

TEST(deliberate_eq_failure)
{
    const int answer = 41;
    CHECK_EQ(answer, 42);
}

TEST(deliberate_near_failure)
{
    CHECK_NEAR(0.75, 0.7071, 0.01);
}

TEST(deliberate_exception)
{
    throw std::runtime_error("boom");
}

TEST(passing_neighbour)
{
    CHECK(true);
}
