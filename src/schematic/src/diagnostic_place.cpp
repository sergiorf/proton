// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/diagnostic_place.h"

#include "proton/schematic/scene_edit.h"

#include "spice_line.h"

#include <algorithm>
#include <string>

namespace proton::schematic {

namespace {

using detail::lower;

bool inBox(const Box& box, Point p)
{
    return p.x >= box.left && p.x <= box.right && p.y >= box.top && p.y <= box.bottom;
}

// Wires are horizontal or vertical, or else straight; `p` on the segment, ends included.
bool onWire(const Wire& wire, Point p)
{
    const long long cross = static_cast<long long>(wire.to.x - wire.from.x) * (p.y - wire.from.y) -
                            static_cast<long long>(wire.to.y - wire.from.y) * (p.x - wire.from.x);
    return cross == 0 && p.x >= std::min(wire.from.x, wire.to.x) &&
           p.x <= std::max(wire.from.x, wire.to.x) && p.y >= std::min(wire.from.y, wire.to.y) &&
           p.y <= std::max(wire.from.y, wire.to.y);
}

void addPin(DiagnosticPlace& place, Point pin)
{
    if (std::ranges::find(place.pins, pin) == place.pins.end()) {
        place.pins.push_back(pin);
    }
}

void locateNet(const Scene& scene, const Conversion& conversion, const std::string& net,
               DiagnosticPlace& place)
{
    for (std::size_t i = 0; i < scene.wires.size() && i < conversion.wireNets.size(); ++i) {
        if (lower(conversion.wireNets[i]) == net) {
            place.wires.push_back(i);
        }
    }
    for (std::size_t i = 0; i < scene.labels.size() && i < conversion.labelNets.size(); ++i) {
        if (lower(conversion.labelNets[i]) == net || lower(scene.labels[i].name) == net) {
            place.labels.push_back(i);
        }
    }
    for (std::size_t i = 0;
         i < scene.parts.size() && i < conversion.circuit.components.size(); ++i) {
        const std::vector<Point> offsets = pinOffsets(scene.parts[i].kind);
        const std::vector<std::string>& nets = conversion.circuit.components[i].nets;
        for (std::size_t p = 0; p < offsets.size() && p < nets.size(); ++p) {
            if (lower(nets[p]) == net) {
                addPin(place, schematic::place(offsets[p], scene.parts[i].placement));
            }
        }
    }
    for (std::size_t i = 0; i < scene.instruments.size() && i < conversion.instruments.size();
         ++i) {
        const std::vector<Point> offsets = terminalOffsets(scene.instruments[i].kind);
        const std::vector<std::string>& nets = conversion.instruments[i].nets;
        for (std::size_t t = 0; t < offsets.size() && t < nets.size(); ++t) {
            if (!nets[t].empty() && lower(nets[t]) == net) {
                addPin(place, schematic::place(offsets[t], scene.instruments[i].placement));
            }
        }
    }
}

void locateElement(const Scene& scene, const Conversion& conversion,
                   const Diagnostic& diagnostic, DiagnosticPlace& place)
{
    const std::string element = lower(diagnostic.element);
    if (!element.empty() || diagnostic.code == DiagnosticCode::InvalidDesignator) {
        for (std::size_t i = 0; i < scene.parts.size(); ++i) {
            const PlacedPart& part = scene.parts[i];
            if (part.kind != ComponentKind::Ground && lower(part.designator) == element) {
                place.parts.push_back(i);
            }
        }
    }
    if (element.empty()) {
        return;
    }
    for (std::size_t i = 0; i < scene.instruments.size(); ++i) {
        const PlacedInstrument& instrument = scene.instruments[i];
        const std::string name = lower(instrument.name);
        if (name != element && element.substr(1) != name) {
            continue;
        }
        place.instruments.push_back(i);
        if (diagnostic.code != DiagnosticCode::UnconnectedTerminal ||
            i >= conversion.instruments.size()) {
            continue;
        }
        const std::vector<Point> offsets = terminalOffsets(instrument.kind);
        const std::vector<std::string>& nets = conversion.instruments[i].nets;
        for (std::size_t t = 0; t < offsets.size() && t < nets.size(); ++t) {
            if (nets[t].empty()) {
                addPin(place, schematic::place(offsets[t], instrument.placement));
            }
        }
    }
}

} // namespace

DiagnosticPlace locate(const Scene& scene, const Conversion& conversion,
                       const Diagnostic& diagnostic)
{
    DiagnosticPlace place;
    if (!diagnostic.net.empty()) {
        locateNet(scene, conversion, lower(diagnostic.net), place);
    } else {
        locateElement(scene, conversion, diagnostic, place);
    }
    return place;
}

bool contains(const Scene& scene, const DiagnosticPlace& place, Point p)
{
    return std::ranges::find(place.pins, p) != place.pins.end() ||
           std::ranges::any_of(place.wires,
                               [&](std::size_t i) {
                                   return i < scene.wires.size() && onWire(scene.wires[i], p);
                               }) ||
           std::ranges::any_of(place.labels,
                               [&](std::size_t i) {
                                   return i < scene.labels.size() &&
                                          scene.labels[i].position == p;
                               }) ||
           std::ranges::any_of(place.parts,
                               [&](std::size_t i) {
                                   return i < scene.parts.size() &&
                                          inBox(bounds(scene.parts[i]), p);
                               }) ||
           std::ranges::any_of(place.instruments, [&](std::size_t i) {
               return i < scene.instruments.size() && inBox(bounds(scene.instruments[i]), p);
           });
}

} // namespace proton::schematic
