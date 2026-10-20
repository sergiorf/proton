// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/core/format.h"

#include "proton/test/test.h"

#include <limits>
#include <string>
#include <string_view>

using proton::core::formatEngineering;
using proton::core::formatMeterReading;
using proton::core::MeterDisplay;

namespace {

struct EngineeringCase
{
    double value;
    int digits;
    std::string_view unit;
    std::string_view expected;
};

struct MeterCase
{
    double value;
    double range;
    std::string_view expectedText;
    std::string_view expectedUnit;
    bool overload;
};

void checkMeter(const MeterCase& c)
{
    const MeterDisplay display = formatMeterReading(c.value, c.range, "V");
    CHECK_EQ(display.text, c.expectedText);
    CHECK_EQ(display.unit, c.expectedUnit);
    CHECK_EQ(display.overload, c.overload);
}

} // namespace

TEST(engineering_uses_every_si_prefix)
{
    const EngineeringCase cases[] = {
        {1.5e-24, 3, "F", "1.50 yF"}, {1.5e-21, 3, "F", "1.50 zF"}, {1.5e-18, 3, "F", "1.50 aF"},
        {1.5e-15, 3, "F", "1.50 fF"}, {1.5e-12, 3, "F", "1.50 pF"}, {1.5e-9, 3, "F", "1.50 nF"},
        {1.5e-6, 3, "F", "1.50 \xC2\xB5"
                         "F"},
        {1.5e-3, 3, "F", "1.50 mF"},  {1.5, 3, "F", "1.50 F"},      {1.5e3, 3, "F", "1.50 kF"},
        {1.5e6, 3, "F", "1.50 MF"},   {1.5e9, 3, "F", "1.50 GF"},   {1.5e12, 3, "F", "1.50 TF"},
        {1.5e15, 3, "F", "1.50 PF"},  {1.5e18, 3, "F", "1.50 EF"},  {1.5e21, 3, "F", "1.50 ZF"},
        {1.5e24, 3, "F", "1.50 YF"},
    };
    for (const EngineeringCase& c : cases) {
        CHECK_EQ(formatEngineering(c.value, c.digits, c.unit), c.expected);
    }
}

TEST(engineering_keeps_fixed_significant_digits)
{
    const EngineeringCase cases[] = {
        {1234.5, 3, "V", "1.23 kV"},  {12345.0, 3, "V", "12.3 kV"}, {123456.0, 3, "V", "123 kV"},
        {0.5, 3, "", "500 m"},        {2.0, 4, "V", "2.000 V"},     {47e-6, 2, "F", "47 \xC2\xB5"
                                                                                   "F"},
        {123.0, 2, "V", "120 V"},     {100.0, 1, "V", "100 V"},     {0.012345, 5, "A", "12.345 mA"},
        {1.0, 1, "", "1"},            {7.0, 3, "", "7.00"},
    };
    for (const EngineeringCase& c : cases) {
        CHECK_EQ(formatEngineering(c.value, c.digits, c.unit), c.expected);
    }
}

TEST(engineering_rounding_carries_into_the_next_prefix)
{
    CHECK_EQ(formatEngineering(999.96, 4, "Hz"), "1.000 kHz");
    CHECK_EQ(formatEngineering(999.4, 3, "Hz"), "999 Hz");
    CHECK_EQ(formatEngineering(0.0099996, 3, "s"), "10.0 ms");
    CHECK_EQ(formatEngineering(9.9996e-4, 3, "s"), "1.00 ms");
}

TEST(engineering_negative_zero_and_special_values)
{
    CHECK_EQ(formatEngineering(-1234.5, 3, "V"), "-1.23 kV");
    CHECK_EQ(formatEngineering(-0.002, 2, "A"), "-2.0 mA");
    CHECK_EQ(formatEngineering(0.0, 3, "V"), "0.00 V");
    CHECK_EQ(formatEngineering(-0.0, 3, "V"), "0.00 V");
    CHECK_EQ(formatEngineering(0.0, 3), "0.00");
    CHECK_EQ(formatEngineering(std::numeric_limits<double>::quiet_NaN(), 3, "V"), "NaN");
    CHECK_EQ(formatEngineering(std::numeric_limits<double>::infinity(), 3, "V"), "Inf");
    CHECK_EQ(formatEngineering(-std::numeric_limits<double>::infinity(), 3, "V"), "-Inf");
}

TEST(engineering_outside_prefix_range_uses_scientific)
{
    CHECK_EQ(formatEngineering(1.234e30, 3, "V"), "1.23e+30 V");
    CHECK_EQ(formatEngineering(1.234e-27, 3, "V"), "1.23e-27 V");
    CHECK_EQ(formatEngineering(9.99e26, 3, "V"), "999 YV");
}

TEST(engineering_clamps_significant_digits)
{
    CHECK_EQ(formatEngineering(1234.5, 0, "V"), "1 kV");
    CHECK_EQ(formatEngineering(1234.5, -3, "V"), "1 kV");
    CHECK_EQ(formatEngineering(0.1, 40), "100.00000000000001 m"); // 17 digits
}

TEST(meter_places_decimal_point_by_range)
{
    const MeterCase cases[] = {
        {0.1234, 2.0, "0.1234", "V", false},     {0.1234, 0.2, "123.40", "mV", false},
        {1.2345, 2.0, "1.2345", "V", false},     {12.345, 20.0, "12.345", "V", false},
        {123.45, 200.0, "123.45", "V", false},   {1234.5, 2000.0, "1.2345", "kV", false},
        {5.0, 20.0, "5.000", "V", false},        {0.0, 2.0, "0.0000", "V", false},
        {0.0005, 2.0, "0.0005", "V", false},     {0.00001, 0.2, "0.01", "mV", false},
        {1e-3, 2e-3, "1.0000", "mV", false},     {-1.2345, 2.0, "-1.2345", "V", false},
        {-0.00004, 2.0, "0.0000", "V", false},
    };
    for (const MeterCase& c : cases) {
        checkMeter(c);
    }
}

TEST(meter_overloads_beyond_19999_counts)
{
    const MeterCase cases[] = {
        {1.9999, 2.0, "1.9999", "V", false},  {1.99994, 2.0, "1.9999", "V", false},
        {1.99996, 2.0, "OL", "V", true},      {2.0, 2.0, "OL", "V", true},
        {-1.9999, 2.0, "-1.9999", "V", false}, {-2.5, 2.0, "OL", "V", true},
        {0.25, 0.2, "OL", "mV", true},        {25.0, 20.0, "OL", "V", true},
    };
    for (const MeterCase& c : cases) {
        checkMeter(c);
    }
    CHECK_EQ(proton::core::kOverloadText, std::string_view{"OL"});
}

TEST(meter_non_finite_values_and_bad_ranges)
{
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const MeterCase cases[] = {
        {inf, 2.0, "OL", "V", true}, {-inf, 0.2, "OL", "mV", true}, {nan, 2.0, "OL", "V", true},
        {1.0, 0.0, "OL", "V", true}, {1.0, -2.0, "OL", "V", true},  {1.0, nan, "OL", "V", true},
        {1.0, inf, "OL", "V", true},
    };
    for (const MeterCase& c : cases) {
        checkMeter(c);
    }
}

TEST(meter_snaps_range_to_nearest_two_times_decade)
{
    checkMeter({1.2345, 1.5, "1.2345", "V", false});
    checkMeter({1.2345, 3.0, "1.2345", "V", false});
    checkMeter({12.345, 10.0, "12.345", "V", false}); // log-nearest to 20 V
}
