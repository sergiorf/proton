// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/circuit.h"

#include "proton/test/test.h"

using proton::schematic::Circuit;
using proton::schematic::Component;
using proton::schematic::ComponentKind;
using proton::schematic::Fragment;
using proton::schematic::kGroundNet;
using proton::schematic::terminalCount;

namespace {

// A source through a 1k / 100n low-pass, with a generator fragment driving the input.
Circuit lowPass()
{
    Circuit circuit;
    circuit.components = {
        {ComponentKind::Resistor, "R1", {"in", "out"}, "1k"},
        {ComponentKind::Capacitor, "C1", {"out", "gnd"}, "100n"},
        {ComponentKind::Ground, "GND1", {"gnd"}, ""},
    };
    circuit.fragments = {
        {"gen1", {"Vgen1 gen1_int gnd SIN(0 1 1k)", "Rgen1 gen1_int in 50"}},
    };
    return circuit;
}

} // namespace

TEST(terminal_count_is_two_except_ground)
{
    CHECK_EQ(terminalCount(ComponentKind::Resistor), 2);
    CHECK_EQ(terminalCount(ComponentKind::Capacitor), 2);
    CHECK_EQ(terminalCount(ComponentKind::Inductor), 2);
    CHECK_EQ(terminalCount(ComponentKind::VoltageSource), 2);
    CHECK_EQ(terminalCount(ComponentKind::CurrentSource), 2);
    CHECK_EQ(terminalCount(ComponentKind::Ground), 1);
}

TEST(ground_net_is_spice_node_zero)
{
    CHECK_EQ(kGroundNet, "0");
}

TEST(circuit_keeps_components_and_fragments_in_order)
{
    const Circuit circuit = lowPass();
    CHECK_EQ(circuit.components.size(), 3);
    CHECK_EQ(circuit.components[0].designator, "R1");
    CHECK_EQ(circuit.components[1].designator, "C1");
    CHECK_EQ(circuit.components[2].designator, "GND1");
    CHECK_EQ(circuit.fragments.size(), 1);
    CHECK_EQ(circuit.fragments[0].name, "gen1");
    CHECK_EQ(circuit.fragments[0].lines.size(), 2);
    CHECK_EQ(circuit.fragments[0].lines[1], "Rgen1 gen1_int in 50");
}

TEST(circuits_compare_by_value)
{
    CHECK(lowPass() == lowPass());

    Circuit changedValue = lowPass();
    changedValue.components[0].value = "2.2k";
    CHECK(changedValue != lowPass());

    Circuit changedNet = lowPass();
    changedNet.components[1].nets[1] = "out";
    CHECK(changedNet != lowPass());

    Circuit changedFragment = lowPass();
    changedFragment.fragments[0].lines.pop_back();
    CHECK(changedFragment != lowPass());

    CHECK(Circuit{} == Circuit{});
    CHECK(Fragment{} == Fragment{});
    CHECK(Component{} == Component{});
}
