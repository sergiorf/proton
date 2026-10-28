// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/project/project.h"

#include "proton/bench/sheet_bench.h"
#include "proton/schematic/netlist.h"
#include "proton/test/test.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace proton;
using project::Loaded;
using project::Project;
using schematic::ComponentKind;
using schematic::InstrumentKind;
using schematic::Rotation;

namespace {

// Every kind of item and every setting away from its default, so a field that is not written or
// not read shows up as a difference.
Project everything()
{
    Project p;
    p.name = "Everything";
    p.description = "Each field set, with \"quotes\" and a\nnew line.";
    p.scene.parts = {
        {ComponentKind::Resistor, "R1", "4.7k", {{4, 0}, Rotation::R0, false}},
        {ComponentKind::Capacitor, "C1", "100n", {{8, 2}, Rotation::R90, true}},
        {ComponentKind::Inductor, "L1", "10m", {{-3, -7}, Rotation::R180, false}},
        {ComponentKind::VoltageSource, "V1", "5", {{0, 2}, Rotation::R270, false}},
        {ComponentKind::CurrentSource, "I1", "1m", {{20, 2}, Rotation::R0, true}},
        {ComponentKind::Ground, "", "", {{8, 6}, Rotation::R0, false}},
    };
    p.scene.instruments = {
        {InstrumentKind::Scope, "scope1", {{17, -1}, Rotation::R0, false}},
        {InstrumentKind::Multimeter, "dmm1", {{16, 7}, Rotation::R90, true}},
        {InstrumentKind::Generator, "gen1", {{-6, 1}, Rotation::R0, false}},
        {InstrumentKind::Supply, "psu1", {{-14, 1}, Rotation::R0, false}},
    };
    p.scene.wires = {{{0, 0}, {2, 0}}, {{6, 0}, {8, 0}}, {{8, 4}, {8, 6}}};
    p.scene.junctions = {{8, 0}, {-1, -2}};
    p.scene.labels = {{"in", {0, 0}}, {"out", {8, 0}}};

    bench::Bench::GeneratorSettings generator;
    generator.config = {instruments::FunctionGenerator::Waveform::Triangle, 1592.0, 0.25, -1.5,
                        50.0, false};
    bench::Bench::ScopeSettings scope;
    scope.config.timePerDiv = 2e-6;
    scope.config.triggerPosition = -2.5;
    scope.config.mode = instruments::Scope::Mode::Single;
    scope.config.deadTime = 1e-3;
    scope.config.trigger = {instruments::Scope::TriggerSource::Channel2, -0.3,
                            instruments::Scope::Slope::Falling, 0.25};
    scope.config.channels[0] = {0.2, 0.1, instruments::ScopeFrontEnd::Coupling::Ac, true, 1e6,
                                true, 10, false};
    scope.config.channels[1].coupling = instruments::ScopeFrontEnd::Coupling::Gnd;
    scope.config.channels[1].voltsPerDiv = 5.0;
    bench::Bench::MultimeterSettings meter;
    meter.config = {instruments::Multimeter::Mode::AcVoltage,
                    instruments::Multimeter::Range::Millivolts200, 0.5};
    p.settings.generators = {generator};
    p.settings.scopes = {scope};
    p.settings.multimeters = {meter};
    bench::Bench::SupplySettings supply;
    supply.config.channels[0] = {12.5, 0.25, false};
    supply.config.channels[1] = {3.3, 2.0, true};
    p.settings.supplies = {supply};
    return p;
}

std::string minimal(const std::string& body)
{
    return R"({"format": "proton-project", "version": 1)" + body + "}";
}

bool errorSays(const Loaded& loaded, const std::string& words)
{
    return !loaded.project && loaded.error.find(words) != std::string::npos;
}

} // namespace

TEST(write_then_read_gives_the_same_project)
{
    const Project original = everything();
    const Loaded loaded = project::read(project::write(original));
    CHECK(loaded.error.empty());
    CHECK(loaded.project.has_value());
    if (loaded.project) {
        CHECK(loaded.project->scene == original.scene);
        CHECK(loaded.project->settings == original.settings);
        CHECK(*loaded.project == original);
    }
}

TEST(the_same_project_always_gives_the_same_text)
{
    const std::string text = project::write(everything());
    CHECK_EQ(project::write(everything()), text);
    CHECK_EQ(project::write(*project::read(text).project), text);
    CHECK(text.ends_with("}\n"));
    // One line per part: a moved part is a one-line change.
    CHECK(text.find(R"({"kind": "resistor", "designator": "R1", "value": "4.7k", "x": 4, "y": 0, )"
                    R"("rotation": 0, "mirrored": false})") != std::string::npos);
}

TEST(save_then_load_gives_the_same_scene_netlist_and_settings)
{
    // The plan's round trip: a file on disk, and what the bench runs from it.
    const Project original = everything();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "proton-test-round-trip.proton";
    std::string error;
    CHECK(project::save(path, original, error));
    CHECK(error.empty());
    const Loaded loaded = project::load(path);
    std::filesystem::remove(path);
    CHECK(loaded.project.has_value());
    if (!loaded.project) {
        return;
    }
    const project::Example before = project::example(original);
    const project::Example after = project::example(*loaded.project);
    CHECK(after.scene == before.scene);
    CHECK(after.bench == before.bench);
    bench::Bench beforeBench(before.bench);
    bench::Bench afterBench(after.bench);
    schematic::Circuit beforeCircuit = before.circuit;
    schematic::Circuit afterCircuit = after.circuit;
    beforeBench.contributeTo(beforeCircuit);
    afterBench.contributeTo(afterCircuit);
    CHECK_EQ(schematic::buildNetlist(afterCircuit), schematic::buildNetlist(beforeCircuit));
}

TEST(nets_are_not_stored_the_drawing_gives_them)
{
    Project p = everything();
    p.settings.scopes[0].channels = {"in", "out"};
    p.settings.multimeters[0].plus = "out";
    const Loaded loaded = project::read(project::write(p));
    CHECK(loaded.project && loaded.project->settings == project::withoutNets(p.settings));
    CHECK(project::write(p).find("\"plus\"") == std::string::npos);
}

TEST(missing_keys_take_their_defaults)
{
    const Loaded loaded = project::read(minimal(
        R"(, "scene": {"parts": [{"kind": "resistor", "designator": "R1"}]},)"
        R"( "settings": {"scopes": [{"name": "scope1", "timePerDiv": 1e-4}]})"));
    CHECK(loaded.error.empty());
    if (!loaded.project) {
        return;
    }
    CHECK_EQ(loaded.project->scene.parts.size(), 1u);
    CHECK(loaded.project->scene.parts[0].placement == schematic::Placement{});
    CHECK(loaded.project->scene.parts[0].value.empty());
    instruments::Scope::Config expected;
    expected.timePerDiv = 1e-4;
    CHECK(loaded.project->settings.scopes[0].config == expected);
    // An empty project is a project.
    CHECK(project::read(minimal("")).project == Project{});
}

TEST(text_that_is_not_a_project_says_so)
{
    CHECK(errorSays(project::read("not json"), "not a Proton project: it is not valid JSON"));
    CHECK(errorSays(project::read("[1, 2]"), "not a Proton project"));
    CHECK(errorSays(project::read(R"({"format": "something else", "version": 1})"),
                    "not a Proton project"));
    CHECK(errorSays(project::read(R"({"format": "proton-project"})"), "no format version"));
}

TEST(a_newer_format_asks_for_a_newer_proton)
{
    const Loaded loaded = project::read(R"({"format": "proton-project", "version": 99})");
    CHECK(errorSays(loaded, "made by a newer Proton (format 99)"));
    CHECK(errorSays(loaded, "Update Proton"));
}

TEST(a_damaged_file_says_what_is_wrong)
{
    CHECK(errorSays(project::read(minimal(R"(, "scene": {"parts": [{"kind": "flux-capacitor"}]})")),
                    "'flux-capacitor' is not a known kind"));
    CHECK(errorSays(project::read(minimal(R"(, "scene": {"parts": [{"x": "four"}]})")),
                    "'x' is not a whole number"));
    CHECK(errorSays(project::read(minimal(R"(, "scene": {"parts": [{"rotation": 45}]})")),
                    "'rotation' is not 0, 90, 180 or 270"));
    CHECK(errorSays(project::read(minimal(R"(, "scene": {"wires": [{"from": [1, 2]}]})")),
                    "a wire has no 'from' or 'to'"));
    CHECK(errorSays(project::read(minimal(R"(, "scene": {"junctions": [[1]]})")),
                    "a junction is not a point"));
    CHECK(errorSays(project::read(minimal(
                        R"(, "settings": {"multimeters": [{"range": "2kV"}]})")),
                    "'2kV' is not a known range"));
    CHECK(errorSays(
        project::read(minimal(R"(, "settings": {"scopes": [{"channels": [{}, {}, {}]}]})")),
        "more than two channels"));
    CHECK(errorSays(project::read(minimal(R"(, "scene": [])")), "'scene' is not an object"));
}

TEST(a_file_that_cannot_be_opened_or_written_says_so)
{
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "proton-no-such-dir" / "x.proton";
    CHECK(errorSays(project::load(missing), "Could not open x.proton"));
    std::string error;
    CHECK(!project::save(missing, Project{}, error));
    CHECK(error.find("Could not write x.proton") != std::string::npos);
}

TEST(examples_are_the_project_files_of_a_folder_in_name_order)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "proton-test-examples";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string error;
    Project b;
    b.name = "Second";
    Project a;
    a.name = "First";
    CHECK(project::save(dir / "02-b.proton", b, error));
    CHECK(project::save(dir / "01-a.proton", a, error));
    std::ofstream(dir / "03-broken.proton") << "{";
    std::ofstream(dir / "notes.txt") << "not a project";
    std::vector<std::string> errors;
    const std::vector<project::Example> found = project::examples(dir, &errors);
    std::filesystem::remove_all(dir);
    CHECK_EQ(found.size(), 2u);
    if (found.size() == 2) {
        CHECK_EQ(found[0].name, std::string("First"));
        CHECK_EQ(found[1].name, std::string("Second"));
        CHECK_EQ(found[0].path.filename().string(), std::string("01-a.proton"));
    }
    CHECK(errors.size() == 1 && errors[0].starts_with("03-broken.proton: "));
}
