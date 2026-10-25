// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/bench.h"

#include <algorithm>
#include <string_view>

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

} // namespace

double maxStepFor(const Scope::Config& scope)
{
    return Scope(scope).sampleInterval();
}

void Bench::configure(const Config& config)
{
    if (config.scope != config_.scope) {
        scope_.configure(config.scope);
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

void Bench::contributeTo(schematic::Circuit& circuit)
{
    if (!config_.generatorNet.empty()) {
        const FunctionGenerator generator(config_.generator);
        circuit.fragments.push_back(
            {kGeneratorName,
             generator.toSpice(kGeneratorName, config_.generatorNet, schematic::kGroundNet).lines});
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

const std::vector<ProbeError>& Bench::attach(std::span<const std::string> signalNames)
{
    signalNames_.assign(signalNames.begin(), signalNames.end());
    attached_ = true;
    resolveAll();
    return errors_;
}

void Bench::restart()
{
    scope_.configure(config_.scope);
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
    if (std::ranges::none_of(scopeSources_, missing)) {
        scope_.feed(time, samples(scopeSources_[0], chunk, begin, count),
                    samples(scopeSources_[1], chunk, begin, count));
    }
    for (std::size_t i = 0; i < multimeters_.size(); ++i) {
        if (!missing(multimeterSources_[i])) {
            multimeters_[i].feed(time, samples(multimeterSources_[i], chunk, begin, count));
        }
    }
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
    multimeterSources_.assign(config_.multimeters.size(), Source{});
    scopeSources_ = {};
    if (!attached_) {
        return;
    }
    const auto check = [this](Source& source, std::string probe, const std::string& net) {
        source = resolve(net);
        if (source.kind == Source::Kind::Missing) {
            errors_.push_back({probe, net,
                               probe + " is on net '" + net +
                                   "', which is not in the circuit. Move the probe to a net "
                                   "of the circuit."});
        }
    };
    for (std::size_t c = 0; c < scopeSources_.size(); ++c) {
        check(scopeSources_[c], "CH" + std::to_string(c + 1), config_.scopeNets[c]);
    }
    for (std::size_t i = 0; i < multimeterSources_.size(); ++i) {
        check(multimeterSources_[i], "DMM" + std::to_string(i + 1), config_.multimeters[i].net);
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
