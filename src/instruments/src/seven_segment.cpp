// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/instruments/seven_segment.h"

#include <algorithm>

namespace proton::instruments {

std::uint8_t sevenSegments(char c)
{
    switch (c) {
    case '0': return kSegmentA | kSegmentB | kSegmentC | kSegmentD | kSegmentE | kSegmentF;
    case '1': return kSegmentB | kSegmentC;
    case '2': return kSegmentA | kSegmentB | kSegmentD | kSegmentE | kSegmentG;
    case '3': return kSegmentA | kSegmentB | kSegmentC | kSegmentD | kSegmentG;
    case '4': return kSegmentB | kSegmentC | kSegmentF | kSegmentG;
    case '5': return kSegmentA | kSegmentC | kSegmentD | kSegmentF | kSegmentG;
    case '6': return kSegmentA | kSegmentC | kSegmentD | kSegmentE | kSegmentF | kSegmentG;
    case '7': return kSegmentA | kSegmentB | kSegmentC;
    case '8':
        return kSegmentA | kSegmentB | kSegmentC | kSegmentD | kSegmentE | kSegmentF | kSegmentG;
    case '9': return kSegmentA | kSegmentB | kSegmentC | kSegmentD | kSegmentF | kSegmentG;
    case '-': return kSegmentG;
    case 'O': return kSegmentA | kSegmentB | kSegmentC | kSegmentD | kSegmentE | kSegmentF;
    case 'L': return kSegmentD | kSegmentE | kSegmentF;
    case 'E': return kSegmentA | kSegmentD | kSegmentE | kSegmentF | kSegmentG;
    case 'r': return kSegmentE | kSegmentG;
    default: return 0;
    }
}

std::vector<DisplayCell> layoutDisplay(std::string_view text, std::size_t cells)
{
    std::vector<DisplayCell> filled;
    for (const char c : text) {
        if (c == '.') {
            if (filled.empty()) {
                filled.push_back({});
            }
            filled.back().point = true;
        } else {
            filled.push_back({sevenSegments(c), false});
        }
    }
    std::vector<DisplayCell> result(cells);
    const std::size_t count = std::min(cells, filled.size());
    std::copy(filled.end() - static_cast<std::ptrdiff_t>(count), filled.end(),
              result.end() - static_cast<std::ptrdiff_t>(count));
    return result;
}

} // namespace proton::instruments
