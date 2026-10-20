// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/units.h"

#include "proton/test/test.h"

#include <limits>
#include <string>
#include <string_view>

using proton::schematic::formatSpiceNumber;
using proton::schematic::parseSpiceNumber;

namespace {

struct ParseCase
{
    std::string_view text;
    double expected;
};

struct FormatCase
{
    double value;
    std::string_view expected;
};

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// The parsed value, or NaN when the text is rejected, so a failure prints the value it got.
double parsed(std::string_view text)
{
    return parseSpiceNumber(text).value_or(kNaN);
}

void checkParses(const ParseCase& c)
{
    CHECK_EQ(parsed(c.text), c.expected);
}

// Prints the rejected or accepted text on failure, which a bare has_value() check would not.
void checkRejected(std::string_view text)
{
    const std::string outcome = parseSpiceNumber(text) ? " accepted" : " rejected";
    CHECK_EQ("'" + std::string(text) + "'" + outcome, "'" + std::string(text) + "' rejected");
}

} // namespace

TEST(parse_reads_every_scale_factor)
{
    const ParseCase cases[] = {
        {"1t", 1e12},     {"1g", 1e9},      {"1meg", 1e6},     {"1k", 1e3},     {"1", 1.0},
        {"1m", 1e-3},     {"1u", 1e-6},     {"1n", 1e-9},      {"1p", 1e-12},   {"1f", 1e-15},
        {"2.2t", 2.2e12}, {"4.7g", 4.7e9},  {"10meg", 10e6},   {"4.7k", 4.7e3}, {"33m", 33e-3},
        {"2.2u", 2.2e-6}, {"470n", 470e-9}, {"100p", 100e-12}, {"15f", 15e-15},
    };
    for (const ParseCase& c : cases) {
        checkParses(c);
    }
    CHECK_NEAR(parsed("1mil"), 25.4e-6, 1e-21);
    CHECK_NEAR(parsed("10mil"), 254e-6, 1e-20);
}

TEST(parse_scale_factors_are_case_insensitive)
{
    const ParseCase cases[] = {
        {"1T", 1e12}, {"1G", 1e9},  {"1MEG", 1e6}, {"1Meg", 1e6}, {"1mEg", 1e6},
        {"1K", 1e3},  {"1U", 1e-6}, {"1N", 1e-9},  {"1P", 1e-12}, {"1F", 1e-15},
    };
    for (const ParseCase& c : cases) {
        checkParses(c);
    }
    CHECK_NEAR(parsed("1MIL"), 25.4e-6, 1e-21);
    CHECK_NEAR(parsed("1Mil"), 25.4e-6, 1e-21);
}

TEST(parse_m_is_milli_and_meg_is_mega)
{
    const ParseCase cases[] = {
        {"1M", 1e-3},     {"1m", 1e-3},      {"1MEG", 1e6},       {"1meg", 1e6},
        {"10MHz", 10e-3}, {"10MegHz", 10e6}, {"2.2Mohm", 2.2e-3}, {"2.2Megohm", 2.2e6},
        {"1meter", 1e-3}, {"1Ms", 1e-3},
    };
    for (const ParseCase& c : cases) {
        checkParses(c);
    }
}

TEST(parse_ignores_trailing_units)
{
    const ParseCase cases[] = {
        {"10uF", 10e-6}, {"4.7kohm", 4.7e3}, {"5V", 5.0},     {"1Hz", 1.0},  {"2s", 2.0},
        {"3ohm", 3.0},   {"100nH", 100e-9},  {"12mA", 12e-3}, {"1kHz", 1e3}, {"5Volts", 5.0},
    };
    for (const ParseCase& c : cases) {
        checkParses(c);
    }
}

TEST(parse_reads_a_unit_starting_with_a_scale_letter_as_the_scale)
{
    // SPICE has no farad unit: F is femto, so "10F" is 10 fF, not 10 F.
    CHECK_EQ(parsed("10F"), 10e-15);
    CHECK_EQ(parsed("1Farad"), 1e-15);
    CHECK_EQ(parsed("1T"), 1e12);
    CHECK_EQ(parsed("1Gs"), 1e9);
}

TEST(parse_reads_signs_decimals_and_exponents)
{
    const ParseCase cases[] = {
        {"0", 0.0},    {"42", 42.0},      {"-5", -5.0},      {"+3", 3.0},      {".5", 0.5},
        {"5.", 5.0},   {"-4.7k", -4.7e3}, {"0.001", 1e-3},   {"1e3", 1e3},     {"1E3", 1e3},
        {"1e+3", 1e3}, {"1e-3", 1e-3},    {"2.5e3k", 2.5e6}, {"1e-6meg", 1.0}, {"-1.5E-2u", -15e-9},
        {"0.1", 0.1},  {"1e", 1.0},       {"1ex", 1.0},      {"007", 7.0},
    };
    for (const ParseCase& c : cases) {
        checkParses(c);
    }
}

TEST(parse_rounds_to_the_nearest_double)
{
    // One rounding of the whole decimal value: the same double as a literal.
    CHECK_EQ(parsed("0.1u"), 0.1e-6);
    CHECK_EQ(parsed("3.3n"), 3.3e-9);
    CHECK_EQ(parsed("6.8p"), 6.8e-12);
    CHECK_EQ(parsed("0.30000000000000004"), 0.1 + 0.2);
}

TEST(parse_rejects_invalid_input)
{
    const std::string_view cases[] = {
        "",
        " ",
        "k",
        "meg",
        ".",
        "-",
        "+",
        "abc",
        "1k2",
        "1 k",
        " 1k",
        "1k ",
        "1.2.3",
        "--1",
        "+-1",
        "1,5",
        "1e999",
        "1e-999",
        "1e99999999999",
        "inf",
        "nan",
        "0x10",
        "1\xC2\xB5"
        "F",
        "e3",
        "1k-",
        "1/2",
        "1_000",
        "1\t",
    };
    for (const std::string_view text : cases) {
        checkRejected(text);
    }
}

TEST(format_uses_every_scale_factor)
{
    const FormatCase cases[] = {
        {1e12, "1t"}, {1e9, "1g"},  {1e6, "1meg"}, {1e3, "1k"},   {1.0, "1"},
        {1e-3, "1m"}, {1e-6, "1u"}, {1e-9, "1n"},  {1e-12, "1p"}, {1e-15, "1f"},
    };
    for (const FormatCase& c : cases) {
        CHECK_EQ(formatSpiceNumber(c.value), c.expected);
    }
}

TEST(format_writes_the_shortest_mantissa_in_1_to_1000)
{
    const FormatCase cases[] = {
        {4700.0, "4.7k"},   {2.2e-6, "2.2u"},     {0.5, "500m"},
        {470.0, "470"},     {12345.0, "12.345k"}, {100e-12, "100p"},
        {999e12, "999t"},   {-10.0, "-10"},       {-4.7e3, "-4.7k"},
        {25.4e-6, "25.4u"}, {0.1, "100m"},        {1.5, "1.5"},
        {0.0, "0"},         {-0.0, "0"},          {0.1 + 0.2, "300.00000000000004m"},
    };
    for (const FormatCase& c : cases) {
        CHECK_EQ(formatSpiceNumber(c.value), c.expected);
    }
}

TEST(format_uses_an_exponent_outside_the_scale_factors)
{
    CHECK_EQ(formatSpiceNumber(1e15), "1e15");
    CHECK_EQ(formatSpiceNumber(2.5e18), "2.5e18");
    CHECK_EQ(formatSpiceNumber(-1e100), "-1e100");
    CHECK_EQ(formatSpiceNumber(1e-16), "1e-16");
    CHECK_EQ(formatSpiceNumber(2.5e-18), "2.5e-18");
    CHECK_EQ(formatSpiceNumber(999.9e-18), "9.999e-16");
}

TEST(format_has_no_form_for_non_finite_values)
{
    CHECK_EQ(formatSpiceNumber(kNaN), "");
    CHECK_EQ(formatSpiceNumber(std::numeric_limits<double>::infinity()), "");
    CHECK_EQ(formatSpiceNumber(-std::numeric_limits<double>::infinity()), "");
}

TEST(format_then_parse_gives_back_the_same_double)
{
    const double cases[] = {
        1.0 / 3.0,
        2.0 / 3.0 * 1e-9,
        0.1 + 0.2,
        4.7e3,
        1e-15,
        999.999e9,
        123456789.0,
        6.02214076e23,
        1.602176634e-19,
        -1e-300,
        1.7976931348623157e308,
        std::numeric_limits<double>::min(),
    };
    for (const double value : cases) {
        CHECK_EQ(parsed(formatSpiceNumber(value)), value);
    }
}
