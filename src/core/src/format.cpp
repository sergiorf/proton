// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/core/format.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace proton::core {

namespace {

// SI prefixes from 10^-24 to 10^24; index 8 is no prefix.
constexpr std::array<std::string_view, 17> kPrefixes = {
    "y", "z", "a", "f", "p", "n", "\xC2\xB5", "m", "", "k", "M", "G", "T", "P", "E", "Z", "Y"};
constexpr int kNoPrefix = 8;

int floorDiv3(int n)
{
    return n >= 0 ? n / 3 : -((-n + 2) / 3);
}

std::string withUnit(std::string number, std::string_view prefix, std::string_view unit)
{
    if (!prefix.empty() || !unit.empty()) {
        number += ' ';
        number += prefix;
        number += unit;
    }
    return number;
}

} // namespace

double roundToSignificant(double value, int significantDigits)
{
    if (value == 0.0 || !std::isfinite(value)) {
        return value;
    }
    // Written in scientific notation at that precision and read back: both conversions are exact
    // and locale-free, so the result is the double nearest the rounded decimal.
    std::array<char, 64> buffer{};
    const auto written =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                      std::chars_format::scientific, std::clamp(significantDigits, 1, 17) - 1);
    double rounded = value;
    (void)std::from_chars(buffer.data(), written.ptr, rounded, std::chars_format::scientific);
    return rounded;
}

std::string formatEngineering(double value, int significantDigits, std::string_view unit)
{
    if (std::isnan(value)) {
        return "NaN";
    }
    if (std::isinf(value)) {
        return value > 0 ? "Inf" : "-Inf";
    }
    const int digitCount = std::clamp(significantDigits, 1, 17);

    // Rounding to the significant digits is left to to_chars, which is exact and locale-free;
    // it also carries 9.996 up to 1.00e+01 so the prefix below is chosen after rounding.
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                      std::chars_format::scientific, digitCount - 1);
    const std::string_view scientific(buffer.data(),
                                      static_cast<std::size_t>(result.ptr - buffer.data()));

    const bool negative = scientific.front() == '-';
    const std::size_t ePos = scientific.find('e');
    std::string digits;
    for (const char c : scientific.substr(0, ePos)) {
        if (c >= '0' && c <= '9') {
            digits += c;
        }
    }
    std::string_view exponentText = scientific.substr(ePos + 1);
    if (exponentText.front() == '+') {
        exponentText.remove_prefix(1);
    }
    int exponent = 0;
    (void)std::from_chars(exponentText.data(),exponentText.data() + exponentText.size(), exponent);
    if (value == 0.0) {
        exponent = 0;
    }

    const int prefixIndex = floorDiv3(exponent) + kNoPrefix;
    if (prefixIndex < 0 || prefixIndex >= static_cast<int>(kPrefixes.size())) {
        return withUnit(std::string(scientific), {}, unit);
    }

    // 1 to 3 digits before the decimal point, padded with zeros when there are fewer digits.
    const auto integerDigits = static_cast<std::size_t>(exponent - 3 * floorDiv3(exponent) + 1);
    if (digits.size() < integerDigits) {
        digits.append(integerDigits - digits.size(), '0');
    }
    std::string number = negative && value != 0.0 ? "-" : "";
    number += digits.substr(0, integerDigits);
    if (digits.size() > integerDigits) {
        number += '.';
        number += digits.substr(integerDigits);
    }
    return withUnit(std::move(number), kPrefixes[static_cast<std::size_t>(prefixIndex)], unit);
}

MeterDisplay formatMeterReading(double value, double range, std::string_view unit)
{
    if (!(range > 0.0) || !std::isfinite(range)) {
        return {std::string(kOverloadText), std::string(unit), true};
    }

    // Full scale 2 x 10^k; one count is 10^(k-4). The prefix puts the range in 1..999 units.
    const int k = std::clamp(static_cast<int>(std::lround(std::log10(range / 2.0))), -24, 26);
    const int prefixPower = floorDiv3(k);
    const int decimals = 4 - (k - 3 * prefixPower);
    std::string prefixedUnit(kPrefixes[static_cast<std::size_t>(prefixPower + kNoPrefix)]);
    prefixedUnit += unit;

    if (!std::isfinite(value)) {
        return {std::string(kOverloadText), std::move(prefixedUnit), true};
    }
    // Scale by an exact power of ten so that e.g. 0.1234 V is 1234 counts, not 1233.9999.
    const double scaled = k <= 4 ? value * std::pow(10.0, 4 - k) : value / std::pow(10.0, k - 4);
    if (std::fabs(scaled) >= static_cast<double>(kMeterCounts) + 0.5) {
        return {std::string(kOverloadText), std::move(prefixedUnit), true};
    }
    const long long counts = std::llround(scaled);

    std::string digits = std::to_string(std::llabs(counts));
    const auto width = static_cast<std::size_t>(decimals + 1);
    if (digits.size() < width) {
        digits.insert(0, width - digits.size(), '0');
    }
    std::string text = counts < 0 ? "-" : "";
    text += digits.substr(0, digits.size() - static_cast<std::size_t>(decimals));
    text += '.';
    text += digits.substr(digits.size() - static_cast<std::size_t>(decimals));
    return {std::move(text), std::move(prefixedUnit), false};
}

} // namespace proton::core
