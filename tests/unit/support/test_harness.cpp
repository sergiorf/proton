// SPDX-License-Identifier: AGPL-3.0-only
// Self-tests of the harness: passing checks record nothing, failing checks record one failure
// whose report names the file, the line and both values.
#include "proton/test/test.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace {

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

std::string location(int line)
{
    return "test_harness.cpp:" + std::to_string(line) + ": error in test '";
}

enum class Slope { Rising = 1, Falling = 2 };

} // namespace

TEST(check_passes_on_true)
{
    proton::test::FailureCapture capture;
    CHECK(1 + 1 == 2);
    CHECK_EQ(capture.failures(), 0);
}

TEST(check_reports_expression_file_and_line)
{
    int line = 0;
    std::string text;
    {
        proton::test::FailureCapture capture;
        line = __LINE__ + 1;
        CHECK(1 + 1 == 3);
        CHECK_EQ(capture.failures(), 1);
        text = capture.text();
    }
    CHECK(contains(text, location(line) + "check_reports_expression_file_and_line'"));
    CHECK(contains(text, "CHECK(1 + 1 == 3) failed"));
}

TEST(check_eq_passes_on_equal_values)
{
    proton::test::FailureCapture capture;
    CHECK_EQ(42, 42);
    CHECK_EQ(std::string("abc"), "abc");
    CHECK_EQ(-1, std::int64_t{-1});
    CHECK_EQ(3u, 3);
    CHECK_EQ(capture.failures(), 0);
}

TEST(check_eq_reports_both_values)
{
    int line = 0;
    std::string text;
    {
        proton::test::FailureCapture capture;
        const int answer = 41;
        line = __LINE__ + 1;
        CHECK_EQ(answer, 42);
        CHECK_EQ(capture.failures(), 1);
        text = capture.text();
    }
    CHECK(contains(text, location(line)));
    CHECK(contains(text, "CHECK_EQ(answer, 42) failed"));
    CHECK(contains(text, "value:    41\n"));
    CHECK(contains(text, "expected: 42\n"));
}

TEST(check_eq_compares_signed_and_unsigned_by_value)
{
    proton::test::FailureCapture capture;
    CHECK_EQ(-1, std::numeric_limits<unsigned>::max());
    CHECK_EQ(capture.failures(), 1);
}

TEST(check_eq_prints_strings_bools_chars_and_enums)
{
    std::string text;
    {
        proton::test::FailureCapture capture;
        CHECK_EQ(std::string("volt"), "amp");
        CHECK_EQ(true, false);
        CHECK_EQ('a', 'b');
        CHECK_EQ(Slope::Rising, Slope::Falling);
        CHECK_EQ(capture.failures(), 4);
        text = capture.text();
    }
    CHECK(contains(text, "value:    \"volt\"\n    expected: \"amp\""));
    CHECK(contains(text, "value:    true\n    expected: false"));
    CHECK(contains(text, "value:    'a'\n    expected: 'b'"));
    CHECK(contains(text, "value:    1\n    expected: 2"));
}

TEST(check_eq_prints_floats_exactly)
{
    std::string text;
    {
        proton::test::FailureCapture capture;
        CHECK_EQ(0.1 + 0.2, 0.3);
        text = capture.text();
    }
    // Shortest round-trip form: the values differ, so the printed text must differ too.
    CHECK(contains(text, "value:    0.30000000000000004\n    expected: 0.3\n"));
}

TEST(check_near_passes_within_tolerance)
{
    proton::test::FailureCapture capture;
    CHECK_NEAR(1.0 / std::sqrt(2.0), 0.7071, 1e-4);
    CHECK_NEAR(1.5, 1.0, 0.5); // the tolerance is inclusive
    CHECK_NEAR(-3.0, -3.0, 0.0);
    const double inf = std::numeric_limits<double>::infinity();
    CHECK_NEAR(inf, inf, 0.0);
    CHECK_EQ(capture.failures(), 0);
}

TEST(check_near_reports_values_tolerance_and_difference)
{
    int line = 0;
    std::string text;
    {
        proton::test::FailureCapture capture;
        line = __LINE__ + 1;
        CHECK_NEAR(0.75, 0.5, 0.125);
        CHECK_EQ(capture.failures(), 1);
        text = capture.text();
    }
    CHECK(contains(text, location(line)));
    CHECK(contains(text, "CHECK_NEAR(0.75, 0.5, 0.125) failed"));
    CHECK(contains(text, "value:      0.75\n"));
    CHECK(contains(text, "expected:   0.5\n"));
    CHECK(contains(text, "tolerance:  0.125\n"));
    CHECK(contains(text, "difference: 0.25\n"));
}

TEST(check_near_fails_on_nan_and_opposite_infinities)
{
    proton::test::FailureCapture capture;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK_NEAR(nan, 0.0, 1e9);
    CHECK_NEAR(nan, nan, 1e9);
    CHECK_NEAR(inf, -inf, inf);
    CHECK_EQ(capture.failures(), 3);
}

TEST(captures_nest)
{
    proton::test::FailureCapture outer;
    {
        proton::test::FailureCapture inner;
        CHECK(false);
        CHECK_EQ(inner.failures(), 1);
    }
    CHECK_EQ(outer.failures(), 0);
}
