// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/project/project.h"

#include "proton/bench/sheet_bench.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace proton::project {

namespace {

using Json = nlohmann::ordered_json;
using bench::Bench;
using instruments::DcPowerSupply;
using instruments::FunctionGenerator;
using instruments::Multimeter;
using instruments::Scope;
using instruments::ScopeFrontEnd;
using schematic::ComponentKind;
using schematic::InstrumentKind;
using schematic::Point;
using schematic::Rotation;

constexpr std::string_view kFormat = "proton-project";

// A damaged file: what is wrong with it, for the student.
class FormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// ---- Words for enumerations ------------------------------------------------------------------

template <typename E>
struct Word
{
    E value;
    std::string_view word;
};

constexpr Word<ComponentKind> kComponentKinds[] = {
    {ComponentKind::Resistor, "resistor"},
    {ComponentKind::Capacitor, "capacitor"},
    {ComponentKind::Inductor, "inductor"},
    {ComponentKind::VoltageSource, "voltage-source"},
    {ComponentKind::CurrentSource, "current-source"},
    {ComponentKind::Ground, "ground"},
};
constexpr Word<InstrumentKind> kInstrumentKinds[] = {
    {InstrumentKind::Generator, "generator"},
    {InstrumentKind::Scope, "scope"},
    {InstrumentKind::Multimeter, "multimeter"},
    {InstrumentKind::Supply, "supply"},
};
constexpr Word<FunctionGenerator::Waveform> kWaveforms[] = {
    {FunctionGenerator::Waveform::Sine, "sine"},
    {FunctionGenerator::Waveform::Square, "square"},
    {FunctionGenerator::Waveform::Triangle, "triangle"},
    {FunctionGenerator::Waveform::Dc, "dc"},
};
constexpr Word<Scope::Mode> kScopeModes[] = {
    {Scope::Mode::Auto, "auto"},
    {Scope::Mode::Normal, "normal"},
    {Scope::Mode::Single, "single"},
};
constexpr Word<Scope::TriggerSource> kTriggerSources[] = {
    {Scope::TriggerSource::Channel1, "CH1"},
    {Scope::TriggerSource::Channel2, "CH2"},
};
constexpr Word<Scope::Slope> kSlopes[] = {
    {Scope::Slope::Rising, "rising"},
    {Scope::Slope::Falling, "falling"},
};
constexpr Word<ScopeFrontEnd::Coupling> kCouplings[] = {
    {ScopeFrontEnd::Coupling::Dc, "dc"},
    {ScopeFrontEnd::Coupling::Ac, "ac"},
    {ScopeFrontEnd::Coupling::Gnd, "gnd"},
};
constexpr Word<Multimeter::Mode> kMeterModes[] = {
    {Multimeter::Mode::DcVoltage, "dc-voltage"},
    {Multimeter::Mode::AcVoltage, "ac-voltage"},
    {Multimeter::Mode::DcCurrent, "dc-current"},
};
constexpr Word<Multimeter::Range> kMeterRanges[] = {
    {Multimeter::Range::Auto, "auto"},
    {Multimeter::Range::Millivolts200, "200mV"},
    {Multimeter::Range::Volts2, "2V"},
    {Multimeter::Range::Volts20, "20V"},
    {Multimeter::Range::Volts200, "200V"},
    {Multimeter::Range::Milliamps2, "2mA"},
    {Multimeter::Range::Milliamps20, "20mA"},
    {Multimeter::Range::Milliamps200, "200mA"},
    {Multimeter::Range::Amps2, "2A"},
};

template <typename E, std::size_t N>
std::string wordOf(const Word<E> (&words)[N], E value)
{
    for (const Word<E>& word : words) {
        if (word.value == value) {
            return std::string(word.word);
        }
    }
    return std::string(words[0].word); // values are sanitised; never reached
}

// ---- Reading fields --------------------------------------------------------------------------

void requireObject(const Json& json, std::string_view what)
{
    if (!json.is_object()) {
        throw FormatError(std::string(what) + " is not an object");
    }
}

const Json* find(const Json& object, const char* key)
{
    const auto it = object.find(key);
    return it == object.end() || it->is_null() ? nullptr : &*it;
}

std::string wrongType(const char* key, std::string_view expected)
{
    return std::string("'") + key + "' is not " + std::string(expected);
}

double number(const Json& object, const char* key, double fallback)
{
    const Json* value = find(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_number()) {
        throw FormatError(wrongType(key, "a number"));
    }
    return value->get<double>();
}

int integer(const Json& object, const char* key, int fallback)
{
    const Json* value = find(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_number_integer()) {
        throw FormatError(wrongType(key, "a whole number"));
    }
    return value->get<int>();
}

bool flag(const Json& object, const char* key, bool fallback)
{
    const Json* value = find(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_boolean()) {
        throw FormatError(wrongType(key, "true or false"));
    }
    return value->get<bool>();
}

std::string text(const Json& object, const char* key, std::string fallback = {})
{
    const Json* value = find(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_string()) {
        throw FormatError(wrongType(key, "text"));
    }
    return value->get<std::string>();
}

template <typename E, std::size_t N>
E word(const Json& object, const char* key, const Word<E> (&words)[N], E fallback)
{
    const Json* value = find(object, key);
    if (value == nullptr) {
        return fallback;
    }
    if (!value->is_string()) {
        throw FormatError(wrongType(key, "a word"));
    }
    const std::string name = value->get<std::string>();
    for (const Word<E>& candidate : words) {
        if (candidate.word == name) {
            return candidate.value;
        }
    }
    throw FormatError("'" + name + "' is not a known " + key);
}

// The array at `key`, empty if there is none.
const Json& array(const Json& object, const char* key)
{
    static const Json empty = Json::array();
    const Json* value = find(object, key);
    if (value == nullptr) {
        return empty;
    }
    if (!value->is_array()) {
        throw FormatError(wrongType(key, "a list"));
    }
    return *value;
}

// ---- Scene -----------------------------------------------------------------------------------

Json point(Point p)
{
    return Json::array({p.x, p.y});
}

Point point(const Json& json, std::string_view what)
{
    if (!json.is_array() || json.size() != 2 || !json[0].is_number_integer() ||
        !json[1].is_number_integer()) {
        throw FormatError(std::string(what) + " is not a point [x, y]");
    }
    return {json[0].get<int>(), json[1].get<int>()};
}

int degrees(Rotation rotation)
{
    return 90 * static_cast<int>(rotation);
}

Rotation rotation(const Json& object)
{
    const int value = integer(object, "rotation", 0);
    if (value % 90 != 0 || value < 0 || value >= 360) {
        throw FormatError("'rotation' is not 0, 90, 180 or 270");
    }
    return static_cast<Rotation>(value / 90);
}

void writePlacement(Json& json, const schematic::Placement& placement)
{
    json["x"] = placement.position.x;
    json["y"] = placement.position.y;
    json["rotation"] = degrees(placement.rotation);
    json["mirrored"] = placement.mirrored;
}

schematic::Placement readPlacement(const Json& json)
{
    return {{integer(json, "x", 0), integer(json, "y", 0)}, rotation(json),
            flag(json, "mirrored", false)};
}

Json writeScene(const schematic::Scene& scene)
{
    Json parts = Json::array();
    for (const schematic::PlacedPart& part : scene.parts) {
        Json json;
        json["kind"] = wordOf(kComponentKinds, part.kind);
        json["designator"] = part.designator;
        json["value"] = part.value;
        writePlacement(json, part.placement);
        parts.push_back(std::move(json));
    }
    Json instruments = Json::array();
    for (const schematic::PlacedInstrument& instrument : scene.instruments) {
        Json json;
        json["kind"] = wordOf(kInstrumentKinds, instrument.kind);
        json["name"] = instrument.name;
        writePlacement(json, instrument.placement);
        instruments.push_back(std::move(json));
    }
    Json wires = Json::array();
    for (const schematic::Wire& wire : scene.wires) {
        Json json;
        json["from"] = point(wire.from);
        json["to"] = point(wire.to);
        wires.push_back(std::move(json));
    }
    Json junctions = Json::array();
    for (const Point junction : scene.junctions) {
        junctions.push_back(point(junction));
    }
    Json labels = Json::array();
    for (const schematic::NetLabel& label : scene.labels) {
        Json json;
        json["name"] = label.name;
        json["x"] = label.position.x;
        json["y"] = label.position.y;
        labels.push_back(std::move(json));
    }
    Json json;
    json["parts"] = std::move(parts);
    json["instruments"] = std::move(instruments);
    json["wires"] = std::move(wires);
    json["junctions"] = std::move(junctions);
    json["labels"] = std::move(labels);
    return json;
}

schematic::Scene readScene(const Json& json)
{
    requireObject(json, "'scene'");
    schematic::Scene scene;
    for (const Json& part : array(json, "parts")) {
        requireObject(part, "a part");
        const ComponentKind kind = word(part, "kind", kComponentKinds, ComponentKind::Resistor);
        scene.parts.push_back(
            {kind, text(part, "designator"), text(part, "value"), readPlacement(part)});
    }
    for (const Json& instrument : array(json, "instruments")) {
        requireObject(instrument, "an instrument");
        scene.instruments.push_back(
            {word(instrument, "kind", kInstrumentKinds, InstrumentKind::Scope),
             text(instrument, "name"), readPlacement(instrument)});
    }
    for (const Json& wire : array(json, "wires")) {
        requireObject(wire, "a wire");
        if (find(wire, "from") == nullptr || find(wire, "to") == nullptr) {
            throw FormatError("a wire has no 'from' or 'to'");
        }
        scene.wires.push_back({point(wire["from"], "a wire's 'from'"),
                               point(wire["to"], "a wire's 'to'")});
    }
    for (const Json& junction : array(json, "junctions")) {
        scene.junctions.push_back(point(junction, "a junction"));
    }
    for (const Json& label : array(json, "labels")) {
        requireObject(label, "a label");
        scene.labels.push_back(
            {text(label, "name"), {integer(label, "x", 0), integer(label, "y", 0)}});
    }
    return scene;
}

// ---- Settings --------------------------------------------------------------------------------

Json writeSettings(const Bench::Config& config)
{
    Json generators = Json::array();
    for (const Bench::GeneratorSettings& generator : config.generators) {
        const FunctionGenerator::Config& c = generator.config;
        Json json;
        json["name"] = generator.name;
        json["waveform"] = wordOf(kWaveforms, c.waveform);
        json["frequency"] = c.frequency;
        json["amplitude"] = c.amplitude;
        json["offset"] = c.offset;
        json["outputImpedance"] = c.outputImpedance;
        json["output"] = c.output;
        generators.push_back(std::move(json));
    }
    Json scopes = Json::array();
    for (const Bench::ScopeSettings& scope : config.scopes) {
        const Scope::Config& c = scope.config;
        Json channels = Json::array();
        for (const ScopeFrontEnd::ChannelConfig& channel : c.channels) {
            Json json;
            json["voltsPerDiv"] = channel.voltsPerDiv;
            json["offset"] = channel.offset;
            json["coupling"] = wordOf(kCouplings, channel.coupling);
            json["bandwidthLimit"] = channel.bandwidthLimit;
            json["bandwidth"] = channel.bandwidth;
            json["quantise"] = channel.quantise;
            json["adcBits"] = channel.adcBits;
            json["shown"] = channel.shown;
            channels.push_back(std::move(json));
        }
        Json trigger;
        trigger["source"] = wordOf(kTriggerSources, c.trigger.source);
        trigger["level"] = c.trigger.level;
        trigger["slope"] = wordOf(kSlopes, c.trigger.slope);
        trigger["hysteresis"] = c.trigger.hysteresis;
        Json json;
        json["name"] = scope.name;
        json["timePerDiv"] = c.timePerDiv;
        json["triggerPosition"] = c.triggerPosition;
        json["mode"] = wordOf(kScopeModes, c.mode);
        json["deadTime"] = c.deadTime;
        json["trigger"] = std::move(trigger);
        json["channels"] = std::move(channels);
        scopes.push_back(std::move(json));
    }
    Json multimeters = Json::array();
    for (const Bench::MultimeterSettings& meter : config.multimeters) {
        Json json;
        json["name"] = meter.name;
        json["mode"] = wordOf(kMeterModes, meter.config.mode);
        json["range"] = wordOf(kMeterRanges, meter.config.range);
        json["gateTime"] = meter.config.gateTime;
        multimeters.push_back(std::move(json));
    }
    Json supplies = Json::array();
    for (const Bench::SupplySettings& supply : config.supplies) {
        Json channels = Json::array();
        for (const DcPowerSupply::ChannelConfig& channel : supply.config.channels) {
            Json json;
            json["voltage"] = channel.voltage;
            json["currentLimit"] = channel.currentLimit;
            json["output"] = channel.output;
            channels.push_back(std::move(json));
        }
        Json json;
        json["name"] = supply.name;
        json["channels"] = std::move(channels);
        supplies.push_back(std::move(json));
    }
    Json json;
    json["generators"] = std::move(generators);
    json["scopes"] = std::move(scopes);
    json["multimeters"] = std::move(multimeters);
    json["supplies"] = std::move(supplies);
    return json;
}

Bench::Config readSettings(const Json& json)
{
    requireObject(json, "'settings'");
    Bench::Config config;
    for (const Json& item : array(json, "generators")) {
        requireObject(item, "a generator's settings");
        Bench::GeneratorSettings generator;
        FunctionGenerator::Config& c = generator.config;
        generator.name = text(item, "name", generator.name);
        c.waveform = word(item, "waveform", kWaveforms, c.waveform);
        c.frequency = number(item, "frequency", c.frequency);
        c.amplitude = number(item, "amplitude", c.amplitude);
        c.offset = number(item, "offset", c.offset);
        c.outputImpedance = number(item, "outputImpedance", c.outputImpedance);
        c.output = flag(item, "output", c.output);
        config.generators.push_back(std::move(generator));
    }
    for (const Json& item : array(json, "scopes")) {
        requireObject(item, "a scope's settings");
        Bench::ScopeSettings scope;
        Scope::Config& c = scope.config;
        scope.name = text(item, "name", scope.name);
        c.timePerDiv = number(item, "timePerDiv", c.timePerDiv);
        c.triggerPosition = number(item, "triggerPosition", c.triggerPosition);
        c.mode = word(item, "mode", kScopeModes, c.mode);
        c.deadTime = number(item, "deadTime", c.deadTime);
        if (const Json* trigger = find(item, "trigger")) {
            requireObject(*trigger, "'trigger'");
            c.trigger.source = word(*trigger, "source", kTriggerSources, c.trigger.source);
            c.trigger.level = number(*trigger, "level", c.trigger.level);
            c.trigger.slope = word(*trigger, "slope", kSlopes, c.trigger.slope);
            c.trigger.hysteresis = number(*trigger, "hysteresis", c.trigger.hysteresis);
        }
        const Json& channels = array(item, "channels");
        if (channels.size() > c.channels.size()) {
            throw FormatError("a scope has more than two channels");
        }
        for (std::size_t i = 0; i < channels.size(); ++i) {
            const Json& channelJson = channels[i];
            requireObject(channelJson, "a scope channel");
            ScopeFrontEnd::ChannelConfig& channel = c.channels[i];
            channel.voltsPerDiv = number(channelJson, "voltsPerDiv", channel.voltsPerDiv);
            channel.offset = number(channelJson, "offset", channel.offset);
            channel.coupling = word(channelJson, "coupling", kCouplings, channel.coupling);
            channel.bandwidthLimit = flag(channelJson, "bandwidthLimit", channel.bandwidthLimit);
            channel.bandwidth = number(channelJson, "bandwidth", channel.bandwidth);
            channel.quantise = flag(channelJson, "quantise", channel.quantise);
            channel.adcBits = integer(channelJson, "adcBits", channel.adcBits);
            channel.shown = flag(channelJson, "shown", channel.shown);
        }
        config.scopes.push_back(std::move(scope));
    }
    for (const Json& item : array(json, "multimeters")) {
        requireObject(item, "a multimeter's settings");
        Bench::MultimeterSettings meter;
        meter.name = text(item, "name", meter.name);
        meter.config.mode = word(item, "mode", kMeterModes, meter.config.mode);
        meter.config.range = word(item, "range", kMeterRanges, meter.config.range);
        meter.config.gateTime = number(item, "gateTime", meter.config.gateTime);
        config.multimeters.push_back(std::move(meter));
    }
    for (const Json& item : array(json, "supplies")) {
        requireObject(item, "a power supply's settings");
        Bench::SupplySettings supply;
        supply.name = text(item, "name", supply.name);
        const Json& channels = array(item, "channels");
        if (channels.size() > supply.config.channels.size()) {
            throw FormatError("a power supply has more than two channels");
        }
        for (std::size_t i = 0; i < channels.size(); ++i) {
            const Json& channelJson = channels[i];
            requireObject(channelJson, "a power supply channel");
            DcPowerSupply::ChannelConfig& channel = supply.config.channels[i];
            channel.voltage = number(channelJson, "voltage", channel.voltage);
            channel.currentLimit = number(channelJson, "currentLimit", channel.currentLimit);
            channel.output = flag(channelJson, "output", channel.output);
        }
        config.supplies.push_back(std::move(supply));
    }
    return config;
}

// ---- Versions --------------------------------------------------------------------------------

// The migration hook: kMigrations[v - 1] upgrades a document of format version v to v + 1, so
// a file of any earlier version reads as the current one. A new format version adds its step here
// and a test with a file of the version before it.
using Migration = void (*)(Json&);
constexpr std::array<Migration, kFormatVersion - 1> kMigrations{};

void migrate(Json& document, int version)
{
    if constexpr (!kMigrations.empty()) {
        for (int v = version; v < kFormatVersion; ++v) {
            kMigrations[static_cast<std::size_t>(v - 1)](document);
        }
    } else {
        (void)document;
        (void)version;
    }
}

// ---- Text ------------------------------------------------------------------------------------

bool scalar(const Json& json)
{
    return !json.is_object() && !json.is_array();
}

// One line for a part, a wire, a label or a channel: a container of scalars, or of lists of them.
bool oneLine(const Json& json)
{
    return std::ranges::all_of(json, [](const Json& child) {
        return scalar(child) || (child.is_array() && std::ranges::all_of(child, scalar));
    });
}

void print(const Json& json, int indent, std::string& out);

void printInline(const Json& json, std::string& out)
{
    if (scalar(json)) {
        out += json.dump();
        return;
    }
    const bool object = json.is_object();
    out += object ? '{' : '[';
    bool first = true;
    for (auto it = json.begin(); it != json.end(); ++it) {
        out += first ? "" : ", ";
        first = false;
        if (object) {
            out += Json(it.key()).dump() + ": ";
        }
        printInline(*it, out);
    }
    out += object ? '}' : ']';
}

// Two-space indents, but a container that oneLine() accepts, or an empty one, on one line.
void print(const Json& json, int indent, std::string& out)
{
    if (scalar(json) || json.empty() || oneLine(json)) {
        printInline(json, out);
        return;
    }
    const bool object = json.is_object();
    const std::string inner(static_cast<std::size_t>(indent + 2), ' ');
    out += object ? "{\n" : "[\n";
    bool first = true;
    for (auto it = json.begin(); it != json.end(); ++it) {
        out += first ? "" : ",\n";
        first = false;
        out += inner;
        if (object) {
            out += Json(it.key()).dump() + ": ";
        }
        print(*it, indent + 2, out);
    }
    out += '\n' + std::string(static_cast<std::size_t>(indent), ' ') + (object ? '}' : ']');
}

} // namespace

std::string write(const Project& project)
{
    Json json;
    json["format"] = kFormat;
    json["version"] = kFormatVersion;
    json["name"] = project.name;
    json["description"] = project.description;
    json["scene"] = writeScene(project.scene);
    json["settings"] = writeSettings(project.settings);
    std::string out;
    print(json, 0, out);
    return out + "\n";
}

Loaded read(std::string_view contents)
{
    const std::string notAProject = "This file is not a Proton project";
    Json document;
    try {
        document = Json::parse(contents.begin(), contents.end());
    } catch (const nlohmann::json::parse_error& error) {
        return {std::nullopt, notAProject + ": it is not valid JSON (at byte " +
                                  std::to_string(error.byte) + ")."};
    }
    const Json* format = document.is_object() ? find(document, "format") : nullptr;
    if (format == nullptr || !format->is_string() || format->get<std::string>() != kFormat) {
        return {std::nullopt, notAProject + "."};
    }
    const Json* version = find(document, "version");
    if (version == nullptr || !version->is_number_integer() || version->get<long long>() < 1) {
        return {std::nullopt, notAProject + ": it has no format version."};
    }
    if (version->get<long long>() > kFormatVersion) {
        return {std::nullopt,
                "This project was made by a newer Proton (format " +
                    std::to_string(version->get<long long>()) + "); this one reads format " +
                    std::to_string(kFormatVersion) + ". Update Proton to open it."};
    }
    try {
        migrate(document, version->get<int>());
        Project project;
        project.name = text(document, "name");
        project.description = text(document, "description");
        if (const Json* scene = find(document, "scene")) {
            project.scene = readScene(*scene);
        }
        if (const Json* settings = find(document, "settings")) {
            project.settings = readSettings(*settings);
        }
        return {std::move(project), {}};
    } catch (const FormatError& error) {
        return {std::nullopt, std::string("This project file is damaged: ") + error.what() + "."};
    } catch (const nlohmann::json::exception& error) {
        return {std::nullopt, std::string("This project file is damaged: ") + error.what() + "."};
    }
}

bool save(const std::filesystem::path& path, const Project& project, std::string& error)
{
    // Written beside it first, then put in its place, so a failed save leaves the old file whole.
    std::filesystem::path temporary = path;
    temporary += ".saving";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file << write(project);
        file.close();
        if (!file) {
            error = "Could not write " + path.filename().string() + ".";
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    std::error_code code;
    std::filesystem::rename(temporary, path, code);
    if (code) {
        error = "Could not write " + path.filename().string() + ": " + code.message() + ".";
        std::filesystem::remove(temporary, code);
        return false;
    }
    error.clear();
    return true;
}

Loaded load(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {std::nullopt, "Could not open " + path.filename().string() + "."};
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return read(contents.str());
}

Bench::Config withoutNets(Bench::Config config)
{
    for (Bench::GeneratorSettings& generator : config.generators) {
        generator.plus.clear();
        generator.com.clear();
    }
    for (Bench::ScopeSettings& scope : config.scopes) {
        for (std::string& channel : scope.channels) {
            channel.clear();
        }
    }
    for (Bench::MultimeterSettings& meter : config.multimeters) {
        meter.plus.clear();
        meter.com.clear();
    }
    for (Bench::SupplySettings& supply : config.supplies) {
        supply.channels = {};
    }
    return config;
}

Example example(const Project& project, std::filesystem::path path)
{
    const schematic::Conversion conversion = schematic::convert(project.scene);
    return {project.name, project.description, std::move(path), project.scene,
            conversion.circuit, bench::benchFor(conversion.instruments, project.settings)};
}

std::vector<Example> examples(const std::filesystem::path& dir, std::vector<std::string>* errors)
{
    std::vector<std::filesystem::path> files;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (entry.is_regular_file() && entry.path().extension() == kExtension) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files,
                      [](const auto& a, const auto& b) { return a.filename() < b.filename(); });
    std::vector<Example> result;
    for (const std::filesystem::path& file : files) {
        Loaded loaded = load(file);
        if (loaded.project) {
            result.push_back(example(*loaded.project, file));
        } else if (errors != nullptr) {
            errors->push_back(file.filename().string() + ": " + loaded.error);
        }
    }
    return result;
}

} // namespace proton::project
