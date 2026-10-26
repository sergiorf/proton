// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/digit_entry.h"
#include "proton/instruments/function_generator.h"
#include "proton/test/test.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using proton::instruments::DigitEntry;
using proton::instruments::FunctionGenerator;

namespace {

// The generator's frequency: 1 mHz to 10 MHz, to the millihertz, the cursor from 1 mHz to 10 MHz.
DigitEntry frequency(double value, int cursor)
{
    return DigitEntry(-3, 7, FunctionGenerator::kMinFrequency, FunctionGenerator::kMaxFrequency,
                      value, cursor);
}

// Turns the digits one by one, from the largest, to set `target`, as a student dials in a value.
void dialIn(DigitEntry& entry, double target)
{
    const auto count = [&](double v) {
        return static_cast<long long>(std::llround(v / std::pow(10.0, entry.finest())));
    };
    const long long wanted = count(target);
    for (int k = entry.coarsest(); k >= entry.finest(); --k) {
        entry.setCursor(k);
        const long long weight = std::llround(std::pow(10.0, k - entry.finest()));
        const long long clicks = wanted / weight - count(entry.value()) / weight;
        entry.turn(static_cast<int>(clicks));
    }
}

// Deterministic pseudo-random numbers in [0, 1).
class Random
{
public:
    double next()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<double>(state_ >> 8) / 16777216.0;
    }

private:
    std::uint32_t state_ = 4242;
};

} // namespace

TEST(turning_adds_the_digit_under_the_cursor)
{
    DigitEntry entry = frequency(1e3, 2); // 1.000 kHz, the cursor on the hundreds
    CHECK(entry.turn(5));
    CHECK_EQ(entry.value(), 1500.0);
    entry.moveCursor(-1); // to the tens
    CHECK_EQ(entry.cursor(), 1);
    CHECK(entry.turn(9));
    entry.moveCursor(-1);
    CHECK(entry.turn(2));
    CHECK_EQ(entry.value(), 1592.0); // 1.592 kHz, exactly
    CHECK(entry.turn(-3));
    CHECK_EQ(entry.value(), 1589.0);
}

TEST(a_digit_carries_into_the_next_and_borrows_from_it)
{
    DigitEntry up = frequency(999.9, -1); // the cursor on the last digit, tenths of a hertz
    CHECK(up.turn(1));
    CHECK_EQ(up.value(), 1000.0); // 1.000 kHz
    DigitEntry down = frequency(1000.0, 0);
    CHECK(down.turn(-1));
    CHECK_EQ(down.value(), 999.0);
    DigitEntry far = frequency(19.95, -2);
    CHECK(far.turn(7));
    CHECK_EQ(far.value(), 20.02);
}

TEST(turning_stops_at_the_generators_range)
{
    DigitEntry top = frequency(9.5e6, 6);
    CHECK(top.turn(1));
    CHECK_EQ(top.value(), FunctionGenerator::kMaxFrequency); // not 10.5 MHz
    CHECK(!top.turn(1));
    DigitEntry bottom = frequency(0.5, 0);
    CHECK(bottom.turn(-1));
    CHECK_EQ(bottom.value(), FunctionGenerator::kMinFrequency);
    CHECK(!bottom.turn(-3));
    CHECK_EQ(bottom.value(), FunctionGenerator::kMinFrequency);
}

TEST(the_cursor_stays_between_the_finest_and_coarsest_digit)
{
    DigitEntry entry = frequency(1e3, 0);
    entry.moveCursor(20);
    CHECK_EQ(entry.cursor(), 7);
    entry.moveCursor(-30);
    CHECK_EQ(entry.cursor(), -3);
    entry.setCursor(99);
    CHECK_EQ(entry.cursor(), 7);
}

TEST(set_values_are_rounded_to_the_finest_digit_and_kept_in_range)
{
    DigitEntry entry = frequency(1e3, 0);
    entry.setValue(1234.56789);
    CHECK_EQ(entry.value(), 1234.568);
    entry.setValue(1e9);
    CHECK_EQ(entry.value(), 10e6);
    entry.setValue(std::nan(""));
    CHECK_EQ(entry.value(), 10e6); // ignored
}

// What the generator's display shows, and where the cursor's digit is in it.
TEST(the_frequency_shows_four_digits_and_always_the_cursors)
{
    DigitEntry entry = frequency(1592.0, 0);
    DigitEntry::Text text = entry.text("Hz");
    CHECK_EQ(text.text, std::string("1.592 kHz"));
    CHECK_EQ(text.cursor, 4u); // the 2
    entry.setCursor(2);
    CHECK_EQ(entry.text("Hz").cursor, 2u); // the 5
    entry.setCursor(-1); // a finer digit than four show: one more
    CHECK_EQ(entry.text("Hz").text, std::string("1.5920 kHz"));
    CHECK_EQ(entry.text("Hz").cursor, 5u);
    // After a carry on the tenths: 999.9 Hz up one is 1.0000 kHz, the cursor still on the tenths.
    DigitEntry carried = frequency(999.9, -1);
    CHECK_EQ(carried.text("Hz").text, std::string("999.9 Hz"));
    carried.turn(1);
    CHECK_EQ(carried.text("Hz").text, std::string("1.0000 kHz"));
    CHECK_EQ(carried.text("Hz").cursor, 5u);
    // Small and large, with every digit that is not zero.
    CHECK_EQ(frequency(0.001, -3).text("Hz").text, std::string("1.000 mHz"));
    CHECK_EQ(frequency(10e6, 6).text("Hz").text, std::string("10.00 MHz"));
    CHECK_EQ(frequency(1234.567, 3).text("Hz").text, std::string("1.234567 kHz"));
    // The cursor left of the first digit: the display grows to show it, as zeros.
    DigitEntry low = frequency(5.0, 3);
    CHECK_EQ(low.text("Hz").text, std::string("0.005000 kHz"));
    CHECK_EQ(low.text("Hz").cursor, 0u);
}

TEST(a_voltage_shows_volts_to_the_millivolt)
{
    DigitEntry offset(-3, 1, -10.0, 10.0, -1.5, -1, 0);
    CHECK_EQ(offset.text("V").text, std::string("-1.500 V"));
    CHECK_EQ(offset.text("V").cursor, 3u); // the 5
    offset.setCursor(1); // tens of volts: shown as a zero
    CHECK_EQ(offset.text("V").text, std::string("-01.500 V"));
    CHECK_EQ(offset.text("V").cursor, 1u);
    DigitEntry amplitude(-3, 1, 0.0, 10.0, 0.0, -1, 0);
    CHECK_EQ(amplitude.text("V").text, std::string("0.000 V"));
    CHECK(amplitude.turn(5));
    CHECK_EQ(amplitude.value(), 0.5);
    CHECK(amplitude.turn(-6)); // stops at 0 V
    CHECK_EQ(amplitude.value(), 0.0);
    CHECK(!amplitude.turn(-1)); // and stays there
    // Digits finer than the millivolt are not shown.
    CHECK_EQ(amplitude.text("V").text.size(), std::string("0.000 V").size());
}

TEST(each_character_of_the_display_knows_its_digit)
{
    const DigitEntry entry = frequency(1592.0, 0);
    const DigitEntry::Text text = entry.text("Hz");
    CHECK_EQ(text.exponents.size(), text.text.size());
    CHECK(text.exponents[0] == std::optional<int>(3));
    CHECK(!text.exponents[1]); // the point
    CHECK(text.exponents[4] == std::optional<int>(0));
    CHECK(!text.exponents[5]); // the space and the unit
    // So clicking a digit puts the cursor there.
    DigitEntry clicked = frequency(1592.0, 0);
    CHECK(clicked.setCursorAt(2));
    CHECK_EQ(clicked.cursor(), 2);
    CHECK(!clicked.setCursorAt(1)); // the point: no digit
    CHECK_EQ(clicked.cursor(), 2);
}

// Done-when of M4.6: every frequency the display can show is reachable by digits alone, from
// any start, and the display shows it exactly.
TEST(every_frequency_is_reachable_by_digits_alone)
{
    Random random;
    std::vector<double> targets = {1592.0, 0.001, 10e6, 9999999.999, 123.456, 47.3e3, 1.0, 999.9};
    for (int i = 0; i < 300; ++i) {
        // Any count of millihertz from 1 mHz to 10 MHz, spread over the decades.
        const double decades = 10.0 * random.next();
        const auto millihertz = static_cast<long long>(std::pow(10.0, decades));
        targets.push_back(static_cast<double>(std::max(1LL, millihertz)) / 1000.0);
    }
    for (const double start : {1e3, 0.001, 10e6, 12345.678}) {
        for (const double target : targets) {
            DigitEntry entry = frequency(start, 2);
            dialIn(entry, target);
            CHECK_EQ(entry.value(), target);
            // The digits shown are the value's.
            const DigitEntry::Text text = entry.text("Hz");
            const long long count = std::llround(entry.value() * 1000.0);
            for (std::size_t c = 0; c < text.text.size(); ++c) {
                if (text.exponents[c]) {
                    const long long weight =
                        std::llround(std::pow(10.0, *text.exponents[c] + 3));
                    CHECK_EQ(text.text[c] - '0', static_cast<int>(count / weight % 10));
                }
            }
        }
    }
}
