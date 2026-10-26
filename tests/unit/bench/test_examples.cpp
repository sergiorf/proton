// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/examples.h"
#include "proton/bench/sheet_bench.h"
#include "proton/schematic/scene.h"
#include "proton/schematic/netlist.h"
#include "proton/schematic/validate.h"
#include "proton/test/test.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using proton::bench::Bench;
using proton::bench::Example;
using proton::schematic::Circuit;

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// "RC low-pass" -> "rc_low_pass"
std::string fileName(const std::string& name)
{
    std::string result;
    for (const char c : name) {
        result += std::isalnum(static_cast<unsigned char>(c))
                      ? static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
                      : '_';
    }
    return result;
}

// The example's circuit with the bench's contributions, as RunControl runs it.
Circuit withBench(const Example& example)
{
    Circuit circuit = example.circuit;
    Bench bench(example.bench);
    bench.contributeTo(circuit);
    return circuit;
}

} // namespace

TEST(there_are_three_examples_with_distinct_names)
{
    const std::vector<Example> all = proton::bench::examples();
    CHECK_EQ(all.size(), 3u);
    std::set<std::string> names;
    for (const Example& example : all) {
        names.insert(example.name);
    }
    CHECK_EQ(names.size(), all.size());
    CHECK_EQ(all[0].name, std::string("RC low-pass"));
    CHECK_EQ(all[1].name, std::string("Voltage divider"));
    CHECK_EQ(all[2].name, std::string("RC high-pass"));
}

TEST(every_example_is_drawn_and_converts_cleanly)
{
    for (const Example& example : proton::bench::examples()) {
        const proton::schematic::Conversion conversion = proton::schematic::convert(example.scene);
        CHECK(conversion.diagnostics.empty());
        CHECK(conversion.circuit == example.circuit);
        CHECK(!example.scene.parts.empty());
        // The instruments are on the sheet, and the bench is what their wiring gives.
        CHECK(proton::bench::benchFor(conversion.instruments, example.bench) == example.bench);
    }
}

TEST(every_example_validates_without_diagnostics)
{
    for (const Example& example : proton::bench::examples()) {
        const std::vector<proton::schematic::Diagnostic> diagnostics =
            proton::schematic::validate(withBench(example));
        CHECK(diagnostics.empty());
    }
}

// Compares with tests/golden/bench/<name>.cir. On a mismatch the actual text is written to the
// build tree, ready to inspect or copy over the golden file.
TEST(example_netlists_match_the_golden_files)
{
    for (const Example& example : proton::bench::examples()) {
        const std::string name = fileName(example.name) + ".cir";
        const std::string actual = proton::schematic::buildNetlist(withBench(example));
        const std::string expected = readFile(std::filesystem::path(PROTON_GOLDEN_DIR) / name);
        if (actual != expected) {
            const std::filesystem::path dir(PROTON_GOLDEN_ACTUAL_DIR);
            std::filesystem::create_directories(dir);
            std::ofstream(dir / name, std::ios::binary) << actual;
        }
        CHECK_EQ(actual, expected);
    }
}

TEST(every_example_probes_in_and_out)
{
    for (const Example& example : proton::bench::examples()) {
        CHECK_EQ(example.bench.scopes.size(), 1u);
        CHECK_EQ(example.bench.multimeters.size(), 1u);
        // The divider runs on its battery.
        CHECK_EQ(example.bench.generators.size(), example.name == "Voltage divider" ? 0u : 1u);
        if (example.bench.scopes.size() == 1 && example.bench.multimeters.size() == 1) {
            CHECK_EQ(example.bench.scopes[0].channels[0], std::string("in"));
            CHECK_EQ(example.bench.scopes[0].channels[1], std::string("out"));
            CHECK_EQ(example.bench.multimeters[0].plus, std::string("out"));
            CHECK_EQ(example.bench.multimeters[0].com, std::string("0"));
        }
        CHECK(Bench(example.bench).check(example.circuit).empty());
    }
}
