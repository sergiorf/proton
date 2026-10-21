// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/sim/sim_chunk.h"
#include "proton/test/test.h"

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using proton::sim::indexOfSignal;
using proton::sim::SimChunk;

static_assert(!std::is_copy_constructible_v<SimChunk>);
static_assert(std::is_nothrow_move_constructible_v<SimChunk>);
static_assert(std::is_nothrow_move_assignable_v<SimChunk>);

TEST(index_of_signal_finds_names_ignoring_case)
{
    const std::vector<std::string> names{"v(in)", "v(out)", "i(v1)"};
    CHECK(indexOfSignal(names, "v(in)") == 0u);
    CHECK(indexOfSignal(names, "V(Out)") == 1u);
    CHECK(indexOfSignal(names, "I(V1)") == 2u);
}

TEST(index_of_signal_is_empty_for_unknown_names)
{
    const std::vector<std::string> names{"v(in)", "v(out)"};
    CHECK(!indexOfSignal(names, "v(mid)").has_value());
    CHECK(!indexOfSignal(names, "v(ou)").has_value());
    CHECK(!indexOfSignal(names, "").has_value());
    CHECK(!indexOfSignal({}, "v(in)").has_value());
}

TEST(index_of_signal_takes_the_first_match)
{
    const std::vector<std::string> names{"v(a)", "V(A)"};
    CHECK(indexOfSignal(names, "v(a)") == 0u);
}

TEST(chunks_move_and_compare_by_value)
{
    SimChunk a;
    a.time = {0.0, 1e-6};
    a.signals = {{1.0, 2.0}};
    CHECK(!a.empty());
    SimChunk b;
    b.time = a.time;
    b.signals = a.signals;
    CHECK(a == b);
    const SimChunk moved = std::move(b);
    CHECK(moved == a);
    CHECK(SimChunk{}.empty());
}
