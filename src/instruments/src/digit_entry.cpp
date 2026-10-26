// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/digit_entry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace proton::instruments {

namespace {

long long powerOfTen(int exponent)
{
    long long result = 1;
    for (int i = 0; i < exponent; ++i) {
        result *= 10;
    }
    return result;
}

// The largest multiple of 3 not above `exponent`: the SI prefix a digit falls under.
int prefixOf(int exponent)
{
    return exponent >= 0 ? exponent / 3 * 3 : -((-exponent + 2) / 3) * 3;
}

std::string_view prefixName(int exponent)
{
    switch (exponent) {
    case -12:
        return "p";
    case -9:
        return "n";
    case -6:
        return "µ";
    case -3:
        return "m";
    case 3:
        return "k";
    case 6:
        return "M";
    case 9:
        return "G";
    default:
        return "";
    }
}

} // namespace

DigitEntry::DigitEntry(int finest, int coarsest, double min, double max, double value, int cursor,
                       std::optional<int> fixedPrefix)
    : finest_(finest), coarsest_(std::max(finest, coarsest)), fixedPrefix_(fixedPrefix)
{
    const auto toCount = [this](double v) {
        return finest_ < 0 ? std::llround(v * static_cast<double>(powerOfTen(-finest_)))
                           : std::llround(v / static_cast<double>(powerOfTen(finest_)));
    };
    min_ = toCount(std::min(min, max));
    max_ = toCount(std::max(min, max));
    count_ = min_;
    setValue(value);
    setCursor(cursor);
}

double DigitEntry::value() const
{
    // Dividing by an exact power of ten gives the double nearest the decimal.
    return finest_ < 0 ? static_cast<double>(count_) / static_cast<double>(powerOfTen(-finest_))
                       : static_cast<double>(count_) * static_cast<double>(powerOfTen(finest_));
}

void DigitEntry::setValue(double value)
{
    if (!std::isfinite(value)) {
        return;
    }
    const double scaled = finest_ < 0 ? value * static_cast<double>(powerOfTen(-finest_))
                                      : value / static_cast<double>(powerOfTen(finest_));
    const double bounded =
        std::clamp(scaled, static_cast<double>(min_), static_cast<double>(max_));
    count_ = std::clamp(std::llround(bounded), min_, max_);
}

void DigitEntry::setCursor(int exponent)
{
    cursor_ = std::clamp(exponent, finest_, coarsest_);
}

bool DigitEntry::setCursorAt(std::size_t index)
{
    const Text shown = text({});
    if (index >= shown.exponents.size() || !shown.exponents[index]) {
        return false;
    }
    setCursor(*shown.exponents[index]);
    return true;
}

bool DigitEntry::turn(int clicks)
{
    const long long step = powerOfTen(cursor_ - finest_);
    const long long target = std::clamp(count_ + static_cast<long long>(clicks) * step, min_, max_);
    if (target == count_) {
        return false;
    }
    count_ = target;
    return true;
}

DigitEntry::Text DigitEntry::text(std::string_view unit, int significant) const
{
    const long long n = std::llabs(count_);
    const auto digit = [&](int k) {
        return k < finest_ ? 0 : static_cast<int>(n / powerOfTen(k - finest_) % 10);
    };
    // The first and last digits that are not zero.
    std::optional<int> lead;
    std::optional<int> last;
    if (n != 0) {
        int digits = 0;
        for (long long m = n; m != 0; m /= 10) {
            ++digits;
        }
        lead = finest_ + digits - 1;
        for (int k = finest_; k <= *lead; ++k) {
            if (digit(k) != 0) {
                last = k;
                break;
            }
        }
    }

    // The prefix of the largest digit shown, the cursor's if it is left of the value's.
    const int prefix =
        fixedPrefix_ ? *fixedPrefix_ : prefixOf(std::max(lead.value_or(cursor_), cursor_));
    int decimals = 0;
    if (fixedPrefix_) {
        decimals = std::max(0, prefix - finest_); // every digit there is
    } else {
        const int forSignificant = lead ? prefix - (*lead - (significant - 1)) : 0;
        const int forLast = last ? prefix - *last : 0;
        decimals = std::max({0, forSignificant, prefix - cursor_, forLast});
    }
    const int top = std::max({lead.value_or(prefix), cursor_, prefix});

    Text result;
    const auto put = [&](char c, std::optional<int> exponent) {
        result.text += c;
        result.exponents.push_back(exponent);
    };
    if (count_ < 0) {
        put('-', std::nullopt);
    }
    for (int k = top; k >= prefix - decimals; --k) {
        if (k == prefix - 1) {
            put('.', std::nullopt);
        }
        if (k == cursor_) {
            result.cursor = result.text.size();
        }
        put(static_cast<char>('0' + digit(k)), k >= finest_ ? std::optional<int>(k) : std::nullopt);
    }
    put(' ', std::nullopt);
    for (const char c : prefixName(prefix)) {
        put(c, std::nullopt);
    }
    for (const char c : unit) {
        put(c, std::nullopt);
    }
    return result;
}

} // namespace proton::instruments
