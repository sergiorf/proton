// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/bench.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace proton::bench {

namespace {

using instruments::DcPowerSupply;
using instruments::FunctionGenerator;
using instruments::Multimeter;
using instruments::Scope;
using instruments::SpiceFragment;

std::string lower(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return result;
}

schematic::Diagnostic nameError(const std::string& name, std::string message)
{
    return {schematic::Severity::Error, schematic::DiagnosticCode::DuplicateDesignator, name, {},
            std::move(message)};
}

} // namespace

double maxStepFor(const Scope::Config& scope)
{
    return Scope(scope).sampleInterval();
}

void Bench::configure(const Config& config)
{
    scopes_.resize(config.scopes.size());
    for (std::size_t i = 0; i < config.scopes.size(); ++i) {
        if (i >= config_.scopes.size() || config.scopes[i].config != config_.scopes[i].config) {
            scopes_[i].configure(config.scopes[i].config);
        }
    }
    multimeters_.resize(config.multimeters.size());
    for (std::size_t i = 0; i < config.multimeters.size(); ++i) {
        if (i >= config_.multimeters.size() ||
            config.multimeters[i].config != config_.multimeters[i].config) {
            multimeters_[i].configure(config.multimeters[i].config);
        }
    }
    supplies_.resize(config.supplies.size());
    for (std::size_t i = 0; i < config.supplies.size(); ++i) {
        if (i >= config_.supplies.size() ||
            config.supplies[i].config != config_.supplies[i].config) {
            supplies_[i].configure(config.supplies[i].config);
        }
    }
    config_ = config;
    resolveAll();
}

double Bench::maxStep() const
{
    double step = kMaxStepWithoutScope;
    for (std::size_t i = 0; i < config_.scopes.size(); ++i) {
        const double scope = scopes_[i].sampleInterval();
        step = i == 0 ? scope : std::min(step, scope);
    }
    return step;
}

void Bench::contributeTo(schematic::Circuit& circuit)
{
    for (const GeneratorSettings& generator : config_.generators) {
        if (generator.plus.empty() || generator.com.empty()) {
            continue;
        }
        circuit.fragments.push_back(
            {generator.name, FunctionGenerator(generator.config)
                                 .toSpice(generator.name, generator.plus, generator.com)
                                 .lines});
    }
    for (const MultimeterSettings& meter : config_.multimeters) {
        SpiceFragment fragment = Multimeter(meter.config).toSpice(meter.name, meter.plus, meter.com);
        if (!fragment.lines.empty()) {
            circuit.fragments.push_back({meter.name, std::move(fragment.lines)});
        }
    }
    for (const SupplySettings& supply : config_.supplies) {
        SpiceFragment fragment = DcPowerSupply(supply.config).toSpice(supply.name, supply.channels);
        if (!fragment.lines.empty()) {
            circuit.fragments.push_back({supply.name, std::move(fragment.lines)});
        }
    }
    groundNets_ = {std::string(schematic::kGroundNet)};
    for (const schematic::Component& component : circuit.components) {
        if (component.kind == schematic::ComponentKind::Ground) {
            for (const std::string& net : component.nets) {
                groundNets_.push_back(lower(net));
            }
        }
    }
}

std::vector<schematic::Diagnostic> Bench::check(const schematic::Circuit& circuit) const
{
    std::vector<std::string> names;
    for (const GeneratorSettings& generator : config_.generators) {
        names.push_back(generator.name);
    }
    for (const ScopeSettings& scope : config_.scopes) {
        names.push_back(scope.name);
    }
    for (const MultimeterSettings& multimeter : config_.multimeters) {
        names.push_back(multimeter.name);
    }
    for (const SupplySettings& supply : config_.supplies) {
        names.push_back(supply.name);
    }

    std::vector<schematic::Diagnostic> diagnostics;
    std::vector<std::string> seen; // lower case
    for (const std::string& name : names) {
        const std::string key = lower(name);
        if (name.empty()) {
            diagnostics.push_back(nameError(name, "An instrument has no name. Give it one."));
            continue;
        }
        if (std::ranges::find(seen, key) != seen.end()) {
            diagnostics.push_back(nameError(
                name, "Two instruments are called '" + name + "'. Rename one of them."));
            continue;
        }
        seen.push_back(key);
        for (const schematic::Component& component : circuit.components) {
            if (lower(component.designator) == key) {
                diagnostics.push_back(nameError(
                    name, "The instrument '" + name + "' has the name of component '" +
                              component.designator + "'. Rename one of them."));
            }
        }
    }
    return diagnostics;
}

const std::vector<ProbeError>& Bench::attach(std::span<const std::string> signalNames)
{
    signalNames_.assign(signalNames.begin(), signalNames.end());
    attached_ = true;
    resolveAll();
    return errors_;
}

void Bench::restart()
{
    for (std::size_t i = 0; i < scopes_.size(); ++i) {
        scopes_[i].configure(config_.scopes[i].config);
    }
    for (std::size_t i = 0; i < multimeters_.size(); ++i) {
        multimeters_[i].configure(config_.multimeters[i].config);
    }
    for (std::size_t i = 0; i < supplies_.size(); ++i) {
        supplies_[i].configure(config_.supplies[i].config);
    }
}

void Bench::feed(const sim::SimChunk& chunk, std::size_t begin, std::size_t end)
{
    end = std::min(end, chunk.time.size());
    if (begin >= end) {
        return;
    }
    const std::size_t count = end - begin;
    const std::span<const double> time(chunk.time.data() + begin, count);
    const auto missing = [](const Source& source) { return source.kind == Source::Kind::Missing; };
    for (std::size_t i = 0; i < scopes_.size(); ++i) {
        const auto& sources = scopeSources_[i];
        if (std::ranges::none_of(sources, missing)) {
            scopes_[i].feed(time, samples(sources[0], chunk, begin, count),
                            samples(sources[1], chunk, begin, count));
        }
    }
    for (std::size_t i = 0; i < multimeters_.size(); ++i) {
        const auto& sources = multimeterSources_[i];
        if (std::ranges::any_of(sources, missing)) {
            continue;
        }
        const std::span<const double> plus = samples(sources[0], chunk, begin, count);
        // Copied out first: samples() may hand back the same buffer of zeros for both.
        difference_.assign(plus.begin(), plus.end());
        const std::span<const double> com = samples(sources[1], chunk, begin, count);
        for (std::size_t k = 0; k < count; ++k) {
            difference_[k] -= com[k];
        }
        multimeters_[i].feed(time, difference_);
    }
    for (std::size_t i = 0; i < supplies_.size(); ++i) {
        const auto& channels = supplySources_[i];
        const bool resolved = std::ranges::none_of(channels, [&](const SupplyChannelSources& c) {
            return missing(c.plus) || missing(c.minus) || missing(c.current);
        });
        if (!resolved) {
            continue;
        }
        std::array<std::span<const double>, DcPowerSupply::kChannels> voltages;
        std::array<std::span<const double>, DcPowerSupply::kChannels> currents;
        for (std::size_t c = 0; c < DcPowerSupply::kChannels; ++c) {
            std::vector<double>& v = supplyVoltages_[c];
            std::vector<double>& a = supplyCurrents_[c];
            if (!channels[c].connected) {
                // An open output: the set voltage while on, and nothing flows.
                const DcPowerSupply::ChannelConfig& set = supplies_[i].config().channels[c];
                v.assign(count, set.output ? set.voltage : 0.0);
                a.assign(count, 0.0);
            } else {
                const std::span<const double> plus = samples(channels[c].plus, chunk, begin, count);
                v.assign(plus.begin(), plus.end());
                const std::span<const double> minus =
                    samples(channels[c].minus, chunk, begin, count);
                for (std::size_t k = 0; k < count; ++k) {
                    v[k] -= minus[k];
                }
                const std::span<const double> branch =
                    samples(channels[c].current, chunk, begin, count);
                a.resize(count);
                for (std::size_t k = 0; k < count; ++k) {
                    a[k] = -branch[k]; // SPICE's branch current flows into +
                }
            }
            voltages[c] = v;
            currents[c] = a;
        }
        supplies_[i].feed(time, voltages, currents);
    }
}

void Bench::rearmScope(std::size_t scope)
{
    if (scope < scopes_.size()) {
        scopes_[scope].configure(config_.scopes[scope].config);
    }
}

void Bench::forceScope(std::size_t scope)
{
    if (scope < scopes_.size()) {
        scopes_[scope].force();
    }
}

bool Bench::scopeStopped(std::size_t scope) const
{
    return scope < scopes_.size() && scopes_[scope].stopped();
}

std::optional<instruments::ScopeFrame> Bench::popFrame(std::size_t scope)
{
    if (scope >= scopes_.size()) {
        return std::nullopt;
    }
    return scopes_[scope].popFrame();
}

std::optional<instruments::DmmReading> Bench::popReading(std::size_t multimeter)
{
    if (multimeter >= multimeters_.size()) {
        return std::nullopt;
    }
    return multimeters_[multimeter].popReading();
}

std::optional<instruments::SupplyReading> Bench::popSupplyReading(std::size_t supply)
{
    if (supply >= supplies_.size()) {
        return std::nullopt;
    }
    return supplies_[supply].popReading();
}

Bench::Source Bench::resolve(const std::string& net) const
{
    if (net.empty() || std::ranges::find(groundNets_, lower(net)) != groundNets_.end()) {
        return {};
    }
    if (const std::optional<std::size_t> index =
            sim::indexOfSignal(signalNames_, "v(" + net + ")")) {
        return {Source::Kind::Signal, *index};
    }
    return {Source::Kind::Missing, 0};
}

void Bench::resolveAll()
{
    errors_.clear();
    scopeSources_.assign(config_.scopes.size(), {});
    multimeterSources_.assign(config_.multimeters.size(), {});
    supplySources_.assign(config_.supplies.size(), {});
    if (!attached_) {
        return;
    }
    const auto check = [this](Source& source, const std::string& probe, const std::string& net) {
        source = resolve(net);
        if (source.kind == Source::Kind::Missing) {
            errors_.push_back({probe, net,
                               probe + " is on net '" + net +
                                   "', which is not in the circuit. Move it to a net of the "
                                   "circuit."});
        }
    };
    for (std::size_t i = 0; i < config_.scopes.size(); ++i) {
        const ScopeSettings& scope = config_.scopes[i];
        for (std::size_t c = 0; c < instruments::Scope::kChannels; ++c) {
            check(scopeSources_[i][c], scope.name + " CH" + std::to_string(c + 1),
                  scope.channels[c]);
        }
    }
    for (std::size_t i = 0; i < config_.multimeters.size(); ++i) {
        const MultimeterSettings& multimeter = config_.multimeters[i];
        if (multimeter.config.mode != Multimeter::Mode::DcCurrent) {
            check(multimeterSources_[i][0], multimeter.name + " +", multimeter.plus);
            check(multimeterSources_[i][1], multimeter.name + " COM", multimeter.com);
            continue;
        }
        // An ammeter reads its own source's branch current, fed as `+` against a COM of zero. A
        // meter that adds no source (open, or shorted by its leads) has nothing through it.
        if (Multimeter(multimeter.config)
                .toSpice(multimeter.name, multimeter.plus, multimeter.com)
                .lines.empty()) {
            continue;
        }
        const std::string source = Multimeter::sourceName(multimeter.name);
        if (const std::optional<std::size_t> index =
                sim::indexOfSignal(signalNames_, "i(" + source + ")")) {
            multimeterSources_[i][0] = {Source::Kind::Signal, *index};
        } else {
            multimeterSources_[i][0] = {Source::Kind::Missing, 0};
            errors_.push_back({multimeter.name, {},
                               "The simulator gives no current for " + multimeter.name +
                                   ", so it cannot read the current through it."});
        }
    }
    for (std::size_t i = 0; i < config_.supplies.size(); ++i) {
        const SupplySettings& supply = config_.supplies[i];
        for (std::size_t c = 0; c < DcPowerSupply::kChannels; ++c) {
            const DcPowerSupply::Terminals& terminals = supply.channels[c];
            SupplyChannelSources& sources = supplySources_[i][c];
            sources.connected = !terminals.plus.empty() && !terminals.minus.empty();
            if (!sources.connected) {
                continue;
            }
            const std::string channel = supply.name + " CH" + std::to_string(c + 1);
            check(sources.plus, channel + "+", terminals.plus);
            check(sources.minus, channel + "-", terminals.minus);
            // The supply's own source is in every netlist it contributes to; an engine that
            // gives no currents cannot show what it delivers.
            const std::string source = DcPowerSupply::sourceName(supply.name, c);
            if (const std::optional<std::size_t> index =
                    sim::indexOfSignal(signalNames_, "i(" + source + ")")) {
                sources.current = {Source::Kind::Signal, *index};
            } else {
                sources.current = {Source::Kind::Missing, 0};
                errors_.push_back({channel, {},
                                   "The simulator gives no current for " + channel +
                                       ", so its display cannot show what it delivers."});
            }
        }
    }
}

std::span<const double> Bench::samples(const Source& source, const sim::SimChunk& chunk,
                                       std::size_t begin, std::size_t count)
{
    if (source.kind == Source::Kind::Signal && source.index < chunk.signals.size()) {
        return {chunk.signals[source.index].data() + begin, count};
    }
    if (zeros_.size() < count) {
        zeros_.resize(count, 0.0);
    }
    return {zeros_.data(), count};
}

} // namespace proton::bench
