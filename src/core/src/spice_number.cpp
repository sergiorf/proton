// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/core/spice_number.h"

#include <array>
#include <charconv>
#include <cmath>
#include <system_error>

namespace proton::core {

namespace {

constexpr double kMil = 25.4e-6;

// Scale factors written by formatSpiceNumber, from 10^-15 to 10^12; index 5 is no factor.
constexpr std::array<std::string_view, 10> kScaleNames = {"f", "p", "n",   "u", "m",
                                                          "",  "k", "meg", "g", "t"};
constexpr int kNoScale = 5;

// Caps a written exponent so accumulating it cannot overflow; anything this large is out of the
// double range anyway and from_chars reports it.
constexpr int kExponentCap = 100000;

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool isLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

char toLower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool startsWithNoCase(std::string_view text, std::string_view prefix)
{
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (toLower(text[i]) != prefix[i]) {
            return false;
        }
    }
    return true;
}

int floorDiv3(int n)
{
    return n >= 0 ? n / 3 : -((-n + 2) / 3);
}

} // namespace

std::optional<double> parseSpiceNumber(std::string_view text)
{
    std::size_t pos = 0;
    std::string number; // the mantissa as from_chars reads it, then "e" and the total exponent
    if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
        if (text[pos] == '-') {
            number += '-';
        }
        ++pos;
    }

    const std::size_t integerStart = pos;
    while (pos < text.size() && isDigit(text[pos])) {
        ++pos;
    }
    const std::string_view integerDigits = text.substr(integerStart, pos - integerStart);
    std::string_view fractionDigits;
    if (pos < text.size() && text[pos] == '.') {
        const std::size_t fractionStart = ++pos;
        while (pos < text.size() && isDigit(text[pos])) {
            ++pos;
        }
        fractionDigits = text.substr(fractionStart, pos - fractionStart);
    }
    if (integerDigits.empty() && fractionDigits.empty()) {
        return std::nullopt;
    }
    number += integerDigits.empty() ? std::string_view("0") : integerDigits;
    if (!fractionDigits.empty()) {
        number += '.';
        number += fractionDigits;
    }

    // An `e` not followed by digits is not an exponent; it is read below as unit letters.
    int exponent = 0;
    if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
        std::size_t p = pos + 1;
        const bool negative = p < text.size() && text[p] == '-';
        if (p < text.size() && (text[p] == '+' || text[p] == '-')) {
            ++p;
        }
        if (p < text.size() && isDigit(text[p])) {
            while (p < text.size() && isDigit(text[p])) {
                if (exponent < kExponentCap) {
                    exponent = exponent * 10 + (text[p] - '0');
                }
                ++p;
            }
            exponent = negative ? -exponent : exponent;
            pos = p;
        }
    }

    const std::string_view suffix = text.substr(pos);
    for (const char c : suffix) {
        if (!isLetter(c)) {
            return std::nullopt;
        }
    }
    bool mil = false;
    if (startsWithNoCase(suffix, "meg")) {
        exponent += 6;
    } else if (startsWithNoCase(suffix, "mil")) {
        mil = true;
    } else if (!suffix.empty()) {
        switch (toLower(suffix.front())) {
        case 't':
            exponent += 12;
            break;
        case 'g':
            exponent += 9;
            break;
        case 'k':
            exponent += 3;
            break;
        case 'm':
            exponent -= 3;
            break;
        case 'u':
            exponent -= 6;
            break;
        case 'n':
            exponent -= 9;
            break;
        case 'p':
            exponent -= 12;
            break;
        case 'f':
            exponent -= 15;
            break;
        default:
            break; // a unit, ignored
        }
    }

    // One conversion of the whole decimal value, so it is rounded once, to the nearest double.
    number += 'e';
    number += std::to_string(exponent);
    double value = 0.0;
    const auto result = std::from_chars(number.data(), number.data() + number.size(), value);
    if (result.ec != std::errc{} || result.ptr != number.data() + number.size()) {
        return std::nullopt;
    }
    // Standard libraries differ on whether underflow to zero is out_of_range; reject it on all.
    const bool zeroMantissa = integerDigits.find_first_not_of('0') == std::string_view::npos &&
                              fractionDigits.find_first_not_of('0') == std::string_view::npos;
    if (value == 0.0 && !zeroMantissa) {
        return std::nullopt;
    }
    if (mil) {
        value *= kMil;
    }
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::string formatSpiceNumber(double value)
{
    if (!std::isfinite(value)) {
        return {};
    }
    if (value == 0.0) {
        return "0";
    }

    // Shortest round-trip digits, e.g. "-4.7e+03"; the decimal point is then only moved.
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                      std::chars_format::scientific);
    const std::string_view scientific(buffer.data(),
                                      static_cast<std::size_t>(result.ptr - buffer.data()));
    const std::size_t ePos = scientific.find('e');
    std::string digits;
    for (const char c : scientific.substr(0, ePos)) {
        if (isDigit(c)) {
            digits += c;
        }
    }
    std::string_view exponentText = scientific.substr(ePos + 1);
    if (exponentText.front() == '+') {
        exponentText.remove_prefix(1);
    }
    int exponent = 0;
    (void)std::from_chars(exponentText.data(), exponentText.data() + exponentText.size(), exponent);

    std::string text = value < 0.0 ? "-" : "";
    const int scaleIndex = floorDiv3(exponent) + kNoScale;
    if (scaleIndex < 0 || scaleIndex >= static_cast<int>(kScaleNames.size())) {
        text += digits.front();
        if (digits.size() > 1) {
            text += '.';
            text += digits.substr(1);
        }
        text += 'e';
        text += std::to_string(exponent);
        return text;
    }

    // 1 to 3 digits before the decimal point, padded with zeros when there are fewer digits.
    const auto integerDigits = static_cast<std::size_t>(exponent - 3 * floorDiv3(exponent) + 1);
    if (digits.size() < integerDigits) {
        digits.append(integerDigits - digits.size(), '0');
    }
    text += digits.substr(0, integerDigits);
    if (digits.size() > integerDigits) {
        text += '.';
        text += digits.substr(integerDigits);
    }
    text += kScaleNames[static_cast<std::size_t>(scaleIndex)];
    return text;
}

} // namespace proton::core
