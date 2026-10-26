// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace proton::instruments {

// A setting entered digit by digit, as on a bench function generator (PLAN.md M4.6): a cursor
// under one digit of the display, and the knob changing that digit. Qt-free, so the panel only
// draws it and forwards the knob and the arrow keys.
//
// The value is held exactly, as a whole number of its finest digit (1 mHz for a frequency, 1 mV
// for a voltage), between `min` and `max`. The cursor is a digit's exponent: 0 the units, 3 the
// thousands, -1 the tenths, from `finest` to `coarsest`. A click of the knob adds or takes
// 10^cursor, so a digit carries into the next and borrows from it as in arithmetic (999.9 Hz up
// one in the tenths is 1000.0 Hz), and a turn past the range stops at its end.
//
// text() is what the display shows: the value with an SI prefix and at least `significant`
// digits, every digit that is not zero, and always the cursor's digit, as a zero left of the first
// digit when it is there. A frequency picks its prefix from the value; a voltage shown in volts
// passes a fixed prefix of 0 and always shows its millivolts.
class DigitEntry
{
public:
    // The display text and, for each of its bytes, the exponent of the digit it shows (none for
    // the sign, the point, the unit and digits finer than `finest`), and where the cursor's digit
    // is in it.
    struct Text
    {
        std::string text;
        std::vector<std::optional<int>> exponents;
        std::size_t cursor = 0;
    };

    // `fixedPrefix`: the exponent of the prefix the display always uses (0: plain units); none
    // picks it from the value. `min` and `max` are rounded to the finest digit.
    DigitEntry(int finest, int coarsest, double min, double max, double value, int cursor,
               std::optional<int> fixedPrefix = std::nullopt);

    double value() const;
    // Sets the value, rounded to the finest digit and kept in range, the cursor where it was. A
    // value that is not a number is ignored.
    void setValue(double value);

    int cursor() const { return cursor_; }
    int finest() const { return finest_; }
    int coarsest() const { return coarsest_; }
    void setCursor(int exponent);
    // Moves the cursor `places` digits: positive to the left, to larger digits.
    void moveCursor(int places) { setCursor(cursor_ + places); }
    // Puts the cursor on the digit at byte `index` of text(); false, and no move, if no digit is
    // there.
    bool setCursorAt(std::size_t index);

    // Adds `clicks` x 10^cursor, kept in range. Returns true if the value changed.
    bool turn(int clicks);

    Text text(std::string_view unit, int significant = 4) const;

private:
    long long count_ = 0; // the value in units of 10^finest_
    long long min_ = 0;
    long long max_ = 0;
    int finest_ = 0;
    int coarsest_ = 0;
    int cursor_ = 0;
    std::optional<int> fixedPrefix_;
};

} // namespace proton::instruments
