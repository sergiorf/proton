// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include "proton/bench/bench.h"
#include "proton/schematic/circuit.h"
#include "proton/schematic/scene.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace proton::project {

// A project file (PLAN.md M4.11): a drawing with its instruments and how each is set, as the
// student saves it. Qt-free, so the app, the tests and later tools share one format.
//
// The file is JSON (nlohmann/json, docs/LICENSING.md), UTF-8, two-space indents with each part,
// instrument, wire, label and scope channel on one line, keys in a fixed order, so the same
// project always gives the same text and a change to a drawing is a small diff:
//
//     {
//       "format": "proton-project",
//       "version": 1,
//       "name": "RC low-pass",
//       "description": "...",
//       "scene": {"parts": [...], "instruments": [...], "wires": [...], "junctions": [...],
//                 "labels": [...]},
//       "settings": {"generators": [...], "scopes": [...], "multimeters": [...],
//                    "supplies": [...]}
//     }
//
// Settings are stored by instance name, without nets: the drawing says where each instrument is
// wired (ADR 0009), so bench::benchFor() gives the nets again on loading. Enumerations are written
// as words ("resistor", "sine", "rising"), rotations in degrees, numbers as JSON numbers that read
// back to the same double.
//
// Reading is lenient where it is safe and strict where it is not: a missing key takes its default,
// so a hand-written file needs only what it changes, while an unknown word, a wrong type or a file
// of a later format is an error with a sentence for the student, never a guess.
struct Project
{
    std::string name;        // shown in the Examples menu and on the start page
    std::string description; // one or two sentences for the start page; may be empty
    schematic::Scene scene;
    bench::Bench::Config settings; // nets empty; see above

    friend bool operator==(const Project&, const Project&) = default;
};

// The format this Proton writes. A file of an earlier version is migrated on reading.
inline constexpr int kFormatVersion = 1;
// The file extension, with its dot.
inline constexpr std::string_view kExtension = ".proton";

// The project as file text, ending in a newline. Deterministic. Nets in `settings` are not written.
std::string write(const Project& project);

// What reading or loading gives: the project, or why there is none.
struct Loaded
{
    std::optional<Project> project;
    std::string error; // one sentence for the student; empty when `project` is set
};

// Reads file text. The migration hook upgrades a file of an earlier format version one version at
// a time to kFormatVersion before it is read; there are no earlier versions yet. A later version
// is an error ("made by a newer Proton"), as is text that is not a Proton project.
Loaded read(std::string_view contents);

// write() to a file, replacing it. False, with `error` set, if it cannot be written.
bool save(const std::filesystem::path& path, const Project& project, std::string& error);
// read() of a file.
Loaded load(const std::filesystem::path& path);

// The settings to store for the bench `config`: the same instances and settings, nets cleared.
bench::Bench::Config withoutNets(bench::Bench::Config config);

// A ready-made project to start from, with what its drawing gives, as the app's Examples menu and
// start page offer it.
struct Example
{
    std::string name;
    std::string description;
    std::filesystem::path path;
    schematic::Scene scene;
    schematic::Circuit circuit; // schematic::convert(scene).circuit
    bench::Bench::Config bench; // bench::benchFor() the drawing's instruments, with their settings
};

// The project's drawing and bench, as an example.
Example example(const Project& project, std::filesystem::path path = {});

// Every project file in `dir` (not its subfolders), in file name order, so a numbered prefix
// orders them ("01-rc-low-pass.proton"). A file that does not load is left out and its error
// added to `errors`, if given.
std::vector<Example> examples(const std::filesystem::path& dir,
                              std::vector<std::string>* errors = nullptr);

} // namespace proton::project
