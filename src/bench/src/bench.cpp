// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/bench.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace proton::bench {

namespace {

using instruments::FunctionGenerator;
using instruments::Scope;

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
}

void Bench::rearmScope(std::size_t scope)
{
    if (scope < scopes_.size()) {
        scopes_[scope].configure(config_.scopes[scope].config);
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
        check(multimeterSources_[i][0], multimeter.name + " +", multimeter.plus);
        check(multimeterSources_[i][1], multimeter.name + " COM", multimeter.com);
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
