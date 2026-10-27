// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/sheet_bench.h"
#include "proton/schematic/scene.h"
#include "proton/test/test.h"

#include <string>
#include <vector>

using proton::bench::Bench;
using proton::bench::benchFor;
using proton::bench::carryOver;
using proton::bench::remember;
using proton::instruments::Multimeter;
using proton::schematic::InstrumentConnections;
using proton::schematic::InstrumentKind;

namespace {

InstrumentConnections connections(InstrumentKind kind, std::string name,
                                  std::vector<std::string> nets)
{
    return {kind, std::move(name), std::move(nets)};
}

} // namespace

TEST(each_instrument_on_the_sheet_is_an_instance_on_its_nets)
{
    const std::vector<InstrumentConnections> sheet = {
        connections(InstrumentKind::Scope, "scope1", {"in", "out"}),
        connections(InstrumentKind::Multimeter, "dmm1", {"out", "0"}),
        connections(InstrumentKind::Generator, "gen1", {"in", "0"}),
        connections(InstrumentKind::Scope, "scope2", {"out", ""}),
        connections(InstrumentKind::Multimeter, "dmm2", {"in", "out"}),
    };
    const Bench::Config bench = benchFor(sheet, {});
    CHECK_EQ(bench.scopes.size(), 2u);
    CHECK_EQ(bench.multimeters.size(), 2u);
    CHECK_EQ(bench.generators.size(), 1u);
    if (bench.scopes.size() == 2 && bench.multimeters.size() == 2 && bench.generators.size() == 1) {
        CHECK_EQ(bench.scopes[0].name, std::string("scope1"));
        CHECK_EQ(bench.scopes[0].channels[0], std::string("in"));
        CHECK_EQ(bench.scopes[0].channels[1], std::string("out"));
        CHECK_EQ(bench.scopes[1].name, std::string("scope2"));
        CHECK(bench.scopes[1].channels[1].empty()); // CH2 wired to nothing
        CHECK_EQ(bench.multimeters[1].name, std::string("dmm2"));
        CHECK_EQ(bench.multimeters[1].plus, std::string("in"));
        CHECK_EQ(bench.multimeters[1].com, std::string("out"));
        CHECK_EQ(bench.generators[0].plus, std::string("in"));
        CHECK_EQ(bench.generators[0].com, std::string("0"));
        // A new instance has the instrument's default settings.
        CHECK(bench.scopes[1].config == proton::instruments::Scope::Config{});
    }
}

TEST(an_instance_keeps_its_settings_by_name_when_its_wiring_changes)
{
    Bench::Config settings;
    Bench::MultimeterSettings meter;
    meter.name = "dmm1";
    meter.config.mode = Multimeter::Mode::AcVoltage;
    meter.plus = "in";
    meter.com = "0";
    settings.multimeters = {meter};
    // Moved to other nets, and the name spelled in other case.
    const Bench::Config bench =
        benchFor({connections(InstrumentKind::Multimeter, "DMM1", {"out", "in"})}, settings);
    CHECK_EQ(bench.multimeters.size(), 1u);
    if (bench.multimeters.size() == 1) {
        CHECK_EQ(bench.multimeters[0].name, std::string("DMM1"));
        CHECK(bench.multimeters[0].config.mode == Multimeter::Mode::AcVoltage);
        CHECK_EQ(bench.multimeters[0].plus, std::string("out"));
        CHECK_EQ(bench.multimeters[0].com, std::string("in"));
    }
}

TEST(settings_of_another_kind_with_the_same_name_are_not_taken)
{
    Bench::Config settings;
    Bench::ScopeSettings scope;
    scope.name = "x1";
    scope.config.timePerDiv = 5e-6;
    settings.scopes = {scope};
    const Bench::Config bench =
        benchFor({connections(InstrumentKind::Multimeter, "x1", {"a", "b"})}, settings);
    CHECK(bench.scopes.empty());
    CHECK_EQ(bench.multimeters.size(), 1u);
}

TEST(remember_keeps_deleted_instances_so_undo_brings_their_settings_back)
{
    Bench::Config settings;
    Bench::Config current = benchFor({connections(InstrumentKind::Scope, "scope1", {"a", "b"}),
                                      connections(InstrumentKind::Scope, "scope2", {"a", ""})},
                                     settings);
    current.scopes[1].config.timePerDiv = 2e-6; // the student turns scope2's timebase
    remember(settings, current);
    // scope2 is deleted: the bench drops it, the settings keep it.
    current = benchFor({connections(InstrumentKind::Scope, "scope1", {"a", "b"})}, settings);
    CHECK_EQ(current.scopes.size(), 1u);
    remember(settings, current);
    CHECK_EQ(settings.scopes.size(), 2u);
    // Undo puts it back, with its timebase.
    current = benchFor({connections(InstrumentKind::Scope, "scope1", {"a", "b"}),
                        connections(InstrumentKind::Scope, "scope2", {"a", ""})},
                       settings);
    CHECK_EQ(current.scopes.size(), 2u);
    if (current.scopes.size() == 2) {
        CHECK_EQ(current.scopes[1].config.timePerDiv, 2e-6);
    }
}

TEST(a_renamed_instrument_keeps_its_settings)
{
    Bench::Config settings;
    Bench::Config current =
        benchFor({connections(InstrumentKind::Scope, "scope1", {"a", "b"})}, settings);
    current.scopes[0].config.timePerDiv = 2e-6;
    remember(settings, current);
    carryOver(settings, InstrumentKind::Scope, "SCOPE1", "probe");
    carryOver(settings, InstrumentKind::Multimeter, "scope1", "meter"); // no such multimeter
    CHECK(settings.multimeters.empty());
    current = benchFor({connections(InstrumentKind::Scope, "probe", {"a", "b"})}, settings);
    CHECK_EQ(current.scopes.size(), 1u);
    if (current.scopes.size() == 1) {
        CHECK_EQ(current.scopes[0].name, std::string("probe"));
        CHECK_EQ(current.scopes[0].config.timePerDiv, 2e-6);
    }
    // The old name keeps them, for undo.
    current = benchFor({connections(InstrumentKind::Scope, "scope1", {"a", "b"})}, settings);
    CHECK_EQ(current.scopes[0].config.timePerDiv, 2e-6);
}
