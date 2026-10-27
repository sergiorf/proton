// SPDX-License-Identifier: AGPL-3.0-only
// The examples in examples/, as the app loads them: each converts and validates cleanly, gives its
// golden netlist, and probes `in` and `out`.
#include "proton/bench/sheet_bench.h"
#include "proton/project/project.h"
#include "proton/schematic/netlist.h"
#include "proton/schematic/scene.h"
#include "proton/schematic/validate.h"
#include "proton/test/test.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using proton::bench::Bench;
using proton::project::Example;
using proton::schematic::Circuit;

namespace {

std::vector<Example> all()
{
    std::vector<std::string> errors;
    std::vector<Example> examples = proton::project::examples(PROTON_EXAMPLES_DIR, &errors);
    for (const std::string& error : errors) {
        std::printf("%s\n", error.c_str());
    }
    CHECK(errors.empty());
    return examples;
}

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

bool runsOnABattery(const Example& example)
{
    return example.name == "Voltage divider" || example.name == "Loaded divider";
}

} // namespace

TEST(the_examples_load_in_file_name_order_with_distinct_names)
{
    const std::vector<Example> examples = all();
    std::vector<std::string> names;
    for (const Example& example : examples) {
        names.push_back(example.name);
        CHECK(!example.description.empty());
    }
    CHECK(names == (std::vector<std::string>{"RC low-pass", "Voltage divider", "RC high-pass",
                                             "Loaded divider", "RC charging", "RLC resonance"}));
    CHECK_EQ(std::set<std::string>(names.begin(), names.end()).size(), names.size());
}

TEST(every_example_is_drawn_and_converts_cleanly)
{
    for (const Example& example : all()) {
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
    for (const Example& example : all()) {
        const proton::schematic::Conversion conversion = proton::schematic::convert(example.scene);
        CHECK(proton::bench::diagnose(conversion, example.bench).empty());
    }
}

// Compares with tests/golden/examples/<name>.cir. On a mismatch the actual text is written to the
// build tree, ready to inspect or copy over the golden file.
TEST(example_netlists_match_the_golden_files)
{
    for (const Example& example : all()) {
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
    for (const Example& example : all()) {
        CHECK_EQ(example.bench.scopes.size(), 1u);
        CHECK_EQ(example.bench.multimeters.size(), 1u);
        // The dividers run on their battery.
        CHECK_EQ(example.bench.generators.size(), runsOnABattery(example) ? 0u : 1u);
        if (example.bench.scopes.size() == 1 && example.bench.multimeters.size() == 1) {
            CHECK_EQ(example.bench.scopes[0].channels[0], std::string("in"));
            CHECK_EQ(example.bench.scopes[0].channels[1], std::string("out"));
            CHECK_EQ(example.bench.multimeters[0].plus, std::string("out"));
            CHECK_EQ(example.bench.multimeters[0].com, std::string("0"));
        }
        CHECK(Bench(example.bench).check(example.circuit).empty());
    }
}

TEST(every_example_file_is_as_this_proton_writes_it)
{
    // So a file edited by hand or by an older Proton shows up here, and saving an example
    // unchanged gives the same text.
    for (const auto& entry : std::filesystem::directory_iterator(PROTON_EXAMPLES_DIR)) {
        const proton::project::Loaded loaded = proton::project::load(entry.path());
        CHECK(loaded.project.has_value());
        if (loaded.project) {
            CHECK_EQ(proton::project::write(*loaded.project), readFile(entry.path()));
        }
    }
}

namespace {

std::vector<proton::schematic::Diagnostic> diagnoseScene(const proton::schematic::Scene& scene)
{
    const proton::schematic::Conversion conversion = proton::schematic::convert(scene);
    return proton::bench::diagnose(conversion, proton::bench::benchFor(conversion.instruments, {}));
}

bool has(const std::vector<proton::schematic::Diagnostic>& diagnostics,
         proton::schematic::DiagnosticCode code, const std::string& element)
{
    return std::ranges::any_of(diagnostics, [&](const proton::schematic::Diagnostic& d) {
        return d.code == code && d.element == element;
    });
}

} // namespace

TEST(a_drawing_is_diagnosed_as_its_run_would_be)
{
    using proton::schematic::DiagnosticCode;
    const proton::schematic::Scene rc = all()[0].scene; // the RC low-pass
    CHECK(diagnoseScene(rc).empty());

    // An instrument named like a part: the bench's name check.
    proton::schematic::Scene scene = rc;
    for (proton::schematic::PlacedInstrument& instrument : scene.instruments) {
        if (instrument.name == "scope1") {
            instrument.name = "r1";
        }
    }
    CHECK(has(diagnoseScene(scene), DiagnosticCode::DuplicateDesignator, "r1"));

    // The generator's fragment is validated with the circuit: a source named like gen1's.
    scene = rc;
    scene.parts.push_back({proton::schematic::ComponentKind::VoltageSource, "Vgen1", "1",
                           {{30, 30}, proton::schematic::Rotation::R0, false}});
    CHECK(has(diagnoseScene(scene), DiagnosticCode::DuplicateDesignator, "Vgen1"));

    // The conversion's own: a meter terminal wired to nothing.
    scene = rc;
    const auto com = std::ranges::find(scene.wires, proton::schematic::Wire{{17, 9}, {17, 11}});
    CHECK(com != scene.wires.end());
    if (com != scene.wires.end()) {
        scene.wires.erase(com);
    }
    CHECK(has(diagnoseScene(scene), DiagnosticCode::UnconnectedTerminal, "dmm1"));
}
