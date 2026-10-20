// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/netlist.h"

#include "proton/schematic/validate.h"
#include "proton/test/test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

using proton::schematic::buildNetlist;
using proton::schematic::Circuit;
using proton::schematic::ComponentKind;
using proton::schematic::Fragment;
using proton::schematic::validate;

namespace {

// 5 V into a 1k / 100n low-pass.
Circuit rcLowPass()
{
    Circuit circuit;
    circuit.components = {
        {ComponentKind::VoltageSource, "V1", {"in", "gnd"}, "5"},
        {ComponentKind::Resistor, "R1", {"in", "out"}, "1k"},
        {ComponentKind::Capacitor, "C1", {"out", "gnd"}, "100n"},
        {ComponentKind::Ground, "GND1", {"gnd"}, ""},
    };
    return circuit;
}

// The low-pass driven by a function generator; its fragment refers to the student's ground net.
Circuit generatorLowPass()
{
    Circuit circuit = rcLowPass();
    circuit.components.erase(circuit.components.begin());
    circuit.fragments = {
        {"gen1",
         {"* function generator gen1", "Vgen1 gen1_int gnd SIN(0 1 1k)", "",
          "Rgen1 gen1_int in 50"}},
    };
    return circuit;
}

// Every component kind, added out of order, with unnormalised values and mixed-case net names.
Circuit mixedCircuit()
{
    Circuit circuit;
    circuit.components = {
        {ComponentKind::Resistor, "R10", {"Mid", "out"}, "4.7kohm"},
        {ComponentKind::VoltageSource, "V1", {"in", "GND"}, "12V"},
        {ComponentKind::Resistor, "R2", {"in", "mid"}, "1000"},
        {ComponentKind::Ground, "GND1", {"gnd"}, ""},
        {ComponentKind::Inductor, "L1", {"out", "Gnd"}, "10mH"},
        {ComponentKind::Capacitor, "C1", {"out", "0"}, "10uF"},
        {ComponentKind::CurrentSource, "I1", {"gnd", "mid"}, "0.5m"},
        {ComponentKind::Resistor, "R3", {"mid", "gnd"}, "1MEG"},
    };
    circuit.fragments = {
        {"gen2", {"Vgen2 gen2_int GND   PULSE(0 1 0 1u 1u 0.5m 1m)  ", "Rgen2 gen2_int out 50"}},
        {"gen1", {"Vgen1 gen1_int gnd SIN(0 1 1k)", "Rgen1 gen1_int in 50"}},
    };
    return circuit;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Compares with tests/golden/netlist/<name>.cir. On a mismatch the actual text is written to the
// build tree, ready to inspect or copy over the golden file.
void checkGolden(const std::string& name, const Circuit& circuit)
{
    CHECK(validate(circuit).empty()); // golden circuits are clean
    const std::string actual = buildNetlist(circuit);
    const std::string expected =
        readFile(std::filesystem::path(PROTON_GOLDEN_DIR) / (name + ".cir"));
    if (actual != expected) {
        const std::filesystem::path dir(PROTON_GOLDEN_ACTUAL_DIR);
        std::filesystem::create_directories(dir);
        std::ofstream(dir / (name + ".cir"), std::ios::binary) << actual;
    }
    CHECK_EQ(actual, expected);
}

bool hasLineStartingWith(const std::string& text, char c)
{
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.front() == c) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(golden_rc_lowpass)
{
    checkGolden("rc_lowpass", rcLowPass());
}

TEST(golden_generator_lowpass)
{
    checkGolden("generator_lowpass", generatorLowPass());
}

TEST(golden_mixed_circuit)
{
    checkGolden("mixed_circuit", mixedCircuit());
}

TEST(same_circuit_gives_byte_identical_text)
{
    CHECK_EQ(buildNetlist(mixedCircuit()), buildNetlist(mixedCircuit()));
    const Circuit circuit = generatorLowPass();
    CHECK_EQ(buildNetlist(circuit), buildNetlist(circuit));
}

TEST(text_does_not_depend_on_insertion_order)
{
    Circuit reversed = mixedCircuit();
    std::reverse(reversed.components.begin(), reversed.components.end());
    std::reverse(reversed.fragments.begin(), reversed.fragments.end());
    CHECK_EQ(buildNetlist(reversed), buildNetlist(mixedCircuit()));
}

TEST(components_sort_in_natural_order_ignoring_case)
{
    Circuit circuit;
    circuit.components = {
        {ComponentKind::Resistor, "R10", {"a", "0"}, "1"},
        {ComponentKind::Resistor, "r2", {"a", "0"}, "1"},
        {ComponentKind::Resistor, "R1", {"a", "0"}, "1"},
        {ComponentKind::Capacitor, "C3", {"a", "0"}, "1"},
        {ComponentKind::Resistor, "R1a", {"a", "0"}, "1"},
    };
    CHECK_EQ(buildNetlist(circuit), "* Proton netlist\n"
                                    "C3 a 0 1\n"
                                    "R1 a 0 1\n"
                                    "R1a a 0 1\n"
                                    "r2 a 0 1\n"
                                    "R10 a 0 1\n");
}

TEST(values_are_normalised)
{
    Circuit circuit;
    circuit.components = {
        {ComponentKind::Resistor, "R1", {"a", "0"}, "1000"},
        {ComponentKind::Resistor, "R2", {"a", "0"}, "1MEGohm"},
        {ComponentKind::Capacitor, "C1", {"a", "0"}, "10uF"},
        {ComponentKind::Capacitor, "C2", {"a", "0"}, "0.1u"},
        {ComponentKind::VoltageSource, "V1", {"a", "0"}, "-5V"},
        {ComponentKind::CurrentSource, "I1", {"a", "0"}, "1e-3"},
    };
    CHECK_EQ(buildNetlist(circuit), "* Proton netlist\n"
                                    "C1 a 0 10u\n"
                                    "C2 a 0 100n\n"
                                    "I1 a 0 DC 1m\n"
                                    "R1 a 0 1k\n"
                                    "R2 a 0 1meg\n"
                                    "V1 a 0 DC -5\n");
}

TEST(ground_nets_become_node_zero_in_components_and_fragments)
{
    Circuit circuit;
    circuit.components = {
        {ComponentKind::Ground, "GND1", {"earth"}, ""},
        {ComponentKind::Ground, "GND2", {"Chassis"}, ""},
        {ComponentKind::Resistor, "R1", {"a", "EARTH"}, "1k"},
        {ComponentKind::Resistor, "R2", {"a", "chassis"}, "1k"},
    };
    circuit.fragments = {{"gen1", {"Vgen1 a Earth DC 1"}}};
    CHECK_EQ(buildNetlist(circuit), "* Proton netlist\n"
                                    "R1 a 0 1k\n"
                                    "R2 a 0 1k\n"
                                    "* gen1\n"
                                    "Vgen1 a 0 DC 1\n");
}

TEST(fragment_lines_keep_their_text_after_the_nodes)
{
    Circuit circuit;
    circuit.fragments = {{"gen1",
                          {"  * indented comment", "Vgen1 x 0   SIN(0 1 1k)\t ", "   ",
                           "Rgen1  x   gnd  50", ".model ignored D"}}};
    CHECK_EQ(buildNetlist(circuit), "* Proton netlist\n"
                                    "* gen1\n"
                                    "  * indented comment\n"
                                    "Vgen1 x 0   SIN(0 1 1k)\n"
                                    "Rgen1 x gnd  50\n"
                                    ".model ignored D\n");
}

TEST(no_analysis_line_and_no_end)
{
    const std::string text = buildNetlist(generatorLowPass());
    CHECK(!hasLineStartingWith(text, '.'));
    CHECK(text.back() == '\n');
}

TEST(empty_circuit_is_the_title_line)
{
    CHECK_EQ(buildNetlist(Circuit{}), "* Proton netlist\n");
}
