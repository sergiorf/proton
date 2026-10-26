// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/bench/sheet_bench.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace proton::bench {

namespace {

using schematic::InstrumentConnections;
using schematic::InstrumentKind;

bool sameName(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) ==
               std::tolower(static_cast<unsigned char>(y));
    });
}

// The settings of the instance called `name` in `instances`, or a new instance's.
template <typename Settings>
Settings settingsOf(const std::vector<Settings>& instances, const std::string& name)
{
    const auto found = std::ranges::find_if(
        instances, [&](const Settings& instance) { return sameName(instance.name, name); });
    Settings settings = found != instances.end() ? *found : Settings{};
    settings.name = name;
    return settings;
}

std::string netOf(const InstrumentConnections& connections, std::size_t terminal)
{
    return terminal < connections.nets.size() ? connections.nets[terminal] : std::string();
}

template <typename Settings>
void keep(std::vector<Settings>& settings, const std::vector<Settings>& current)
{
    for (const Settings& instance : current) {
        const auto found = std::ranges::find_if(
            settings, [&](const Settings& known) { return sameName(known.name, instance.name); });
        if (found != settings.end()) {
            *found = instance;
        } else {
            settings.push_back(instance);
        }
    }
}

} // namespace

Bench::Config benchFor(const std::vector<InstrumentConnections>& instruments,
                       const Bench::Config& settings)
{
    Bench::Config bench;
    for (const InstrumentConnections& instrument : instruments) {
        switch (instrument.kind) {
        case InstrumentKind::Generator: {
            Bench::GeneratorSettings generator = settingsOf(settings.generators, instrument.name);
            generator.plus = netOf(instrument, 0);
            generator.com = netOf(instrument, 1);
            bench.generators.push_back(std::move(generator));
            break;
        }
        case InstrumentKind::Scope: {
            Bench::ScopeSettings scope = settingsOf(settings.scopes, instrument.name);
            for (std::size_t c = 0; c < scope.channels.size(); ++c) {
                scope.channels[c] = netOf(instrument, c);
            }
            bench.scopes.push_back(std::move(scope));
            break;
        }
        case InstrumentKind::Multimeter: {
            Bench::MultimeterSettings meter = settingsOf(settings.multimeters, instrument.name);
            meter.plus = netOf(instrument, 0);
            meter.com = netOf(instrument, 1);
            bench.multimeters.push_back(std::move(meter));
            break;
        }
        }
    }
    return bench;
}

void remember(Bench::Config& settings, const Bench::Config& current)
{
    keep(settings.generators, current.generators);
    keep(settings.scopes, current.scopes);
    keep(settings.multimeters, current.multimeters);
}

} // namespace proton::bench
