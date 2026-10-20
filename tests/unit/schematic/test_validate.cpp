// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/validate.h"

#include "proton/test/test.h"

#include <string>
#include <vector>

using proton::schematic::Circuit;
using proton::schematic::codeName;
using proton::schematic::ComponentKind;
using proton::schematic::Diagnostic;
using proton::schematic::DiagnosticCode;
using proton::schematic::hasErrors;
using proton::schematic::Severity;
using proton::schematic::validate;

namespace {

// 5 V into a 1k / 100n low-pass.
Circuit lowPass()
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

// The low-pass driven by a generator fragment instead of V1.
Circuit generatorLowPass()
{
    Circuit circuit = lowPass();
    circuit.components.erase(circuit.components.begin());
    circuit.fragments = {
        {"gen1",
         {"* function generator gen1", "Vgen1 gen1_int gnd SIN(0 1 1k)", "",
          "Rgen1 gen1_int in 50"}},
    };
    return circuit;
}

// The codes in order, "NoGround,ZeroValue", so a failure shows all of them.
std::string codes(const std::vector<Diagnostic>& diagnostics)
{
    std::string text;
    for (const Diagnostic& diagnostic : diagnostics) {
        if (!text.empty()) {
            text += ',';
        }
        text += codeName(diagnostic.code);
    }
    return text;
}

std::string codes(const Circuit& circuit)
{
    return codes(validate(circuit));
}

} // namespace

TEST(clean_circuit_has_no_diagnostics)
{
    CHECK_EQ(codes(lowPass()), "");
    CHECK_EQ(codes(generatorLowPass()), "");
    CHECK(!hasErrors(validate(lowPass())));
}

TEST(sources_of_zero_are_valid)
{
    Circuit circuit = lowPass();
    circuit.components[0].value = "0";
    circuit.components.push_back({ComponentKind::CurrentSource, "I1", {"out", "gnd"}, "0"});
    CHECK_EQ(codes(circuit), "");
}

TEST(no_ground)
{
    Circuit circuit = lowPass();
    circuit.components.pop_back();
    const std::vector<Diagnostic> diagnostics = validate(circuit);
    // Without ground every net would lack a DC path; only the cause is reported.
    CHECK_EQ(codes(diagnostics), "NoGround");
    CHECK(diagnostics.front().severity == Severity::Error);
}

TEST(node_zero_is_ground_without_a_ground_symbol)
{
    Circuit circuit = lowPass();
    circuit.components.pop_back();
    circuit.components[0].nets[1] = "0";
    circuit.components[2].nets[1] = "0";
    CHECK_EQ(codes(circuit), "");
}

TEST(invalid_designator)
{
    const char* designators[] = {"", "R 1", "X1", "C1x"};
    for (const char* designator : designators) {
        Circuit circuit = lowPass();
        circuit.components[1].designator = designator;
        CHECK_EQ(codes(circuit), "InvalidDesignator");
    }
    Circuit lowerCase = lowPass();
    lowerCase.components[1].designator = "r1";
    CHECK_EQ(codes(lowerCase), "");
}

TEST(ground_designators_are_not_checked)
{
    Circuit circuit = lowPass();
    circuit.components.push_back({ComponentKind::Ground, "", {"gnd"}, ""});
    circuit.components.push_back({ComponentKind::Ground, "GND1", {"gnd"}, "junk"});
    CHECK_EQ(codes(circuit), "");
}

TEST(duplicate_designator_ignores_case)
{
    Circuit circuit = lowPass();
    circuit.components.push_back({ComponentKind::Resistor, "r1", {"in", "out"}, "2k"});
    const std::vector<Diagnostic> diagnostics = validate(circuit);
    CHECK_EQ(codes(diagnostics), "DuplicateDesignator");
    CHECK_EQ(diagnostics.front().element, "r1");
}

TEST(duplicate_designator_across_components_and_fragments)
{
    Circuit circuit = generatorLowPass();
    circuit.components.push_back({ComponentKind::Resistor, "RGEN1", {"in", "out"}, "2k"});
    const std::vector<Diagnostic> diagnostics = validate(circuit);
    CHECK_EQ(codes(diagnostics), "DuplicateDesignator");
    CHECK_EQ(diagnostics.front().element, "Rgen1");

    Circuit twoGenerators = generatorLowPass();
    twoGenerators.fragments.push_back(twoGenerators.fragments.front());
    twoGenerators.fragments.back().name = "gen2";
    CHECK_EQ(codes(twoGenerators), "DuplicateDesignator,DuplicateDesignator");
}

TEST(invalid_terminals)
{
    Circuit missingNet = lowPass();
    missingNet.components.push_back({ComponentKind::Resistor, "R2", {"out"}, "1k"});
    CHECK_EQ(codes(missingNet), "InvalidTerminals");

    Circuit emptyNet = lowPass();
    emptyNet.components.push_back({ComponentKind::Resistor, "R2", {"out", ""}, "1k"});
    CHECK_EQ(codes(emptyNet), "InvalidTerminals");

    Circuit extraNet = lowPass();
    extraNet.components.push_back({ComponentKind::Capacitor, "C2", {"in", "out", "gnd"}, "1n"});
    CHECK_EQ(codes(extraNet), "InvalidTerminals");

    Circuit groundWithoutNet = lowPass();
    groundWithoutNet.components.push_back({ComponentKind::Ground, "GND2", {}, ""});
    CHECK_EQ(codes(groundWithoutNet), "InvalidTerminals");
}

TEST(invalid_value)
{
    const char* values[] = {"", "abc", "1 k", "1k2", "4,7k"};
    for (const char* value : values) {
        Circuit circuit = lowPass();
        circuit.components[1].value = value;
        CHECK_EQ(codes(circuit), "InvalidValue");
    }
    Circuit source = lowPass();
    source.components[0].value = "five";
    CHECK_EQ(codes(source), "InvalidValue");
}

TEST(zero_value)
{
    Circuit resistor = lowPass();
    resistor.components[1].value = "0";
    CHECK_EQ(codes(resistor), "ZeroValue");

    Circuit capacitor = lowPass();
    capacitor.components[2].value = "0pF";
    CHECK_EQ(codes(capacitor), "ZeroValue");

    Circuit inductor = lowPass();
    inductor.components.push_back({ComponentKind::Inductor, "L1", {"out", "gnd"}, "0.0m"});
    CHECK_EQ(codes(inductor), "ZeroValue");
}

TEST(invalid_fragment_line)
{
    const char* lines[] = {"Xamp a b c opamp", "Vbad a", ".model d1 D", "+ continued"};
    for (const char* line : lines) {
        Circuit circuit = generatorLowPass();
        circuit.fragments.front().lines.push_back(line);
        CHECK_EQ(codes(circuit), "InvalidFragmentLine");
    }
}

TEST(single_connection_net)
{
    Circuit circuit = lowPass();
    circuit.components.push_back({ComponentKind::Resistor, "R2", {"out", "probe"}, "1k"});
    const std::vector<Diagnostic> diagnostics = validate(circuit);
    CHECK_EQ(codes(diagnostics), "SingleConnectionNet");
    CHECK_EQ(diagnostics.front().net, "probe");
    CHECK(diagnostics.front().severity == Severity::Warning);
    CHECK(!hasErrors(diagnostics));
}

TEST(ground_net_with_one_connection_is_fine)
{
    Circuit circuit;
    circuit.components = {
        {ComponentKind::VoltageSource, "V1", {"a", "gnd"}, "1"},
        {ComponentKind::Resistor, "R1", {"a", "a"}, "1k"},
        {ComponentKind::Ground, "GND1", {"gnd"}, ""},
    };
    CHECK_EQ(codes(circuit), "");
}

TEST(no_dc_path_through_capacitors)
{
    Circuit circuit = lowPass();
    circuit.components[1] = {ComponentKind::Capacitor, "C2", {"in", "mid"}, "1u"};
    circuit.components.push_back({ComponentKind::Capacitor, "C3", {"mid", "out"}, "1u"});
    const std::vector<Diagnostic> diagnostics = validate(circuit);
    CHECK_EQ(codes(diagnostics), "NoDcPathToGround,NoDcPathToGround");
    CHECK_EQ(diagnostics[0].net, "mid");
    CHECK_EQ(diagnostics[1].net, "out");
    CHECK(hasErrors(diagnostics));
}

TEST(no_dc_path_through_a_current_source)
{
    Circuit circuit;
    circuit.components = {
        {ComponentKind::CurrentSource, "I1", {"gnd", "a"}, "1m"},
        {ComponentKind::Capacitor, "C1", {"a", "gnd"}, "1u"},
        {ComponentKind::Ground, "GND1", {"gnd"}, ""},
    };
    const std::vector<Diagnostic> diagnostics = validate(circuit);
    CHECK_EQ(codes(diagnostics), "NoDcPathToGround");
    CHECK_EQ(diagnostics.front().net, "a");
}

TEST(no_dc_path_for_a_floating_island)
{
    Circuit circuit = lowPass();
    circuit.components.push_back({ComponentKind::Resistor, "R2", {"x", "y"}, "1k"});
    circuit.components.push_back({ComponentKind::Resistor, "R3", {"y", "x"}, "1k"});
    CHECK_EQ(codes(circuit), "NoDcPathToGround,NoDcPathToGround");
}

TEST(inductors_conduct_at_dc)
{
    // A high-pass: out reaches ground only through L1.
    Circuit circuit = lowPass();
    circuit.components[1] = {ComponentKind::Capacitor, "C2", {"in", "out"}, "1u"};
    circuit.components[2] = {ComponentKind::Inductor, "L1", {"out", "gnd"}, "10m"};
    CHECK_EQ(codes(circuit), "");
}

TEST(net_names_ignore_case)
{
    Circuit circuit = lowPass();
    circuit.components[1].nets[1] = "OUT";
    circuit.components[3].nets[0] = "Gnd";
    CHECK_EQ(codes(circuit), "");
}

TEST(diagnostics_come_in_code_order_and_are_repeatable)
{
    Circuit circuit = lowPass();
    circuit.components.pop_back();           // NoGround
    circuit.components[1].value = "0";       // ZeroValue
    circuit.components[2].designator = "R1"; // InvalidDesignator, DuplicateDesignator
    circuit.components.push_back({ComponentKind::Resistor, "R9", {"in"}, "1k"});
    CHECK_EQ(codes(circuit), "NoGround,InvalidDesignator,DuplicateDesignator,InvalidTerminals,"
                             "ZeroValue");
    CHECK(validate(circuit) == validate(circuit));
}
