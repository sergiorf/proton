// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace proton::instruments {

// A 7-segment digit display, as on a bench multimeter (PLAN.md M2.5), Qt-free so the display's
// content is tested here and the widget only draws segments.
//
// Segments are bits 0 to 6 in the usual lettering: a (top), b (top right), c (bottom right),
// d (bottom), e (bottom left), f (top left), g (middle).
enum Segment : std::uint8_t {
    kSegmentA = 1u << 0,
    kSegmentB = 1u << 1,
    kSegmentC = 1u << 2,
    kSegmentD = 1u << 3,
    kSegmentE = 1u << 4,
    kSegmentF = 1u << 5,
    kSegmentG = 1u << 6,
};

// The segments that show `c`: the digits, '-', and the letters a meter writes ("OL", "Err"):
// O, L, E, r. Anything else, the space included, lights none.
std::uint8_t sevenSegments(char c);

// One digit cell of the display.
struct DisplayCell
{
    std::uint8_t segments = 0;
    bool point = false; // the decimal point after the digit

    friend bool operator==(const DisplayCell&, const DisplayCell&) = default;
};

// `text` ("-0.5902", "123.40", "OL") on a display of `cells` cells, right-aligned as a meter shows
// it: each character takes a cell, except '.', which lights the point of the cell before it. Cells
// left over on the left are blank; characters that do not fit are dropped from the left.
std::vector<DisplayCell> layoutDisplay(std::string_view text, std::size_t cells);

} // namespace proton::instruments
