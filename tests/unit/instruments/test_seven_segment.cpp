// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/seven_segment.h"
#include "proton/test/test.h"

#include <cstdint>
#include <vector>

using proton::instruments::DisplayCell;
using proton::instruments::layoutDisplay;
using proton::instruments::sevenSegments;

namespace {

int lit(std::uint8_t segments)
{
    int count = 0;
    for (int bit = 0; bit < 7; ++bit) {
        count += (segments >> bit) & 1;
    }
    return count;
}

} // namespace

TEST(digits_light_their_usual_segments)
{
    // Segment counts of 0 to 9 on a 7-segment display.
    const int expected[] = {6, 2, 5, 5, 4, 5, 6, 3, 7, 6};
    for (int d = 0; d <= 9; ++d) {
        CHECK_EQ(lit(sevenSegments(static_cast<char>('0' + d))), expected[d]);
    }
    CHECK_EQ(sevenSegments('8'), std::uint8_t{0x7f});
    CHECK_EQ(sevenSegments('-'), std::uint8_t{proton::instruments::kSegmentG});
}

TEST(meter_letters_and_blanks)
{
    CHECK_EQ(sevenSegments('O'), sevenSegments('0'));
    CHECK_EQ(lit(sevenSegments('L')), 3);
    CHECK_EQ(sevenSegments(' '), std::uint8_t{0});
    CHECK_EQ(sevenSegments('x'), std::uint8_t{0});
}

TEST(a_reading_is_right_aligned_with_its_point_on_the_digit_before)
{
    // Sign and five digits in seven cells: one blank on the left.
    const std::vector<DisplayCell> cells = layoutDisplay("-0.5902", 7);
    CHECK_EQ(cells.size(), 7u);
    CHECK_EQ(cells[0].segments, std::uint8_t{0}); // blank
    CHECK_EQ(cells[1].segments, sevenSegments('-'));
    CHECK_EQ(cells[2].segments, sevenSegments('0'));
    CHECK(cells[2].point);
    CHECK_EQ(cells[3].segments, sevenSegments('5'));
    CHECK(!cells[3].point);
    CHECK_EQ(cells[6].segments, sevenSegments('2'));
}

TEST(overload_and_too_long_text)
{
    const std::vector<DisplayCell> ol = layoutDisplay("OL", 6);
    CHECK_EQ(ol[4].segments, sevenSegments('O'));
    CHECK_EQ(ol[5].segments, sevenSegments('L'));
    const std::vector<DisplayCell> cut = layoutDisplay("1234567", 3);
    CHECK_EQ(cut[0].segments, sevenSegments('5'));
    CHECK_EQ(cut[2].segments, sevenSegments('7'));
}
