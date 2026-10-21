// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/sim/sim_chunk.h"

#include <algorithm>

namespace proton::sim {

namespace {

char lowerAscii(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalIgnoringCase(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, [](char x, char y) { return lowerAscii(x) == lowerAscii(y); });
}

} // namespace

std::optional<std::size_t> indexOfSignal(std::span<const std::string> names, std::string_view name)
{
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (equalIgnoringCase(names[i], name)) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace proton::sim
