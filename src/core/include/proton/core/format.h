// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string>
#include <string_view>

namespace proton::core {

// Engineering notation: the exponent is a multiple of three, written as an SI prefix (y to Y),
// with exactly `significantDigits` significant digits (clamped to 1..17), trailing zeros kept.
//
//     formatEngineering(1234.5, 3, "V")  -> "1.23 kV"
//     formatEngineering(0.000047, 2, "F") -> "47 µF"
//     formatEngineering(999.96, 4, "Hz") -> "1.000 kHz"
//     formatEngineering(0.5, 3)          -> "500 m"
//
// The micro sign is UTF-8 "µ" (U+00B5). The value and the prefixed unit are separated by one
// space; with no prefix and no unit there is no space. Magnitudes outside the prefixes' range are
// written in scientific notation ("1.23e+30 V"). NaN is "NaN", infinities "Inf" and "-Inf".
// Output does not depend on the C locale.
std::string formatEngineering(double value, int significantDigits, std::string_view unit = {});

// What a 4.5-digit (19999-count) meter shows. A unit-less overload shows `kOverloadText` with the
// range's unit, as real meters do.
inline constexpr std::string_view kOverloadText = "OL";
inline constexpr long long kMeterCounts = 19999;

struct MeterDisplay
{
    std::string text; // "1.2345", "-199.99", "0.0000" or "OL"
    std::string unit; // range unit with its prefix, "mV", "V", "kV"
    bool overload = false;
};

// Formats `value` on a 4.5-digit meter range whose nominal full scale is `range` = 2 x 10^k
// (0.2, 2, 20, ...; other values snap to the nearest such range in log scale). The decimal point
// is fixed by the range, and readings beyond +-19999 counts, or non-finite ones, are an overload.
//
//     formatMeterReading(0.1234, 2.0, "V")  -> {"0.1234", "V"}
//     formatMeterReading(0.1234, 0.2, "V")  -> {"123.40", "mV"}
//     formatMeterReading(25.0, 20.0, "V")   -> {"OL", "V", overload}
//
// A non-positive or non-finite range is reported as an overload on the base unit.
MeterDisplay formatMeterReading(double value, double range, std::string_view unit);

} // namespace proton::core
