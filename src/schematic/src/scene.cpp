// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/scene.h"

#include "spice_line.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <optional>
#include <utility>

namespace proton::schematic {

namespace {

using detail::lower;

// Union-find over the points of the drawing.
class Points
{
public:
    std::size_t id(Point point)
    {
        const auto [it, inserted] = index_.try_emplace(point, parent_.size());
        if (inserted) {
            parent_.push_back(parent_.size());
            points_.push_back(point);
        }
        return it->second;
    }

    void join(std::size_t a, std::size_t b) { parent_[root(a)] = root(b); }

    std::size_t root(std::size_t id)
    {
        while (parent_[id] != id) {
            parent_[id] = parent_[parent_[id]];
            id = parent_[id];
        }
        return id;
    }

    std::size_t size() const { return parent_.size(); }
    Point point(std::size_t id) const { return points_[id]; }

private:
    std::map<Point, std::size_t> index_;
    std::vector<std::size_t> parent_;
    std::vector<Point> points_;
};

// True if `p` lies on the segment from `a` to `b`, ends included. Exact on the integer grid.
bool onSegment(Point p, Point a, Point b)
{
    const long long cross = static_cast<long long>(b.x - a.x) * (p.y - a.y) -
                            static_cast<long long>(b.y - a.y) * (p.x - a.x);
    return cross == 0 && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) &&
           p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
}

std::vector<Point> placed(const std::vector<Point>& offsets, const Placement& placement)
{
    std::vector<Point> result;
    result.reserve(offsets.size());
    for (const Point offset : offsets) {
        result.push_back(place(offset, placement));
    }
    return result;
}

std::string instrumentKindName(InstrumentKind kind)
{
    switch (kind) {
    case InstrumentKind::Generator:
        return "generator";
    case InstrumentKind::Scope:
        return "scope";
    case InstrumentKind::Multimeter:
        return "multimeter";
    }
    return "instrument";
}

} // namespace

std::vector<Point> pinOffsets(ComponentKind kind)
{
    switch (kind) {
    case ComponentKind::Resistor:
    case ComponentKind::Capacitor:
    case ComponentKind::Inductor:
        return {{-2, 0}, {2, 0}};
    case ComponentKind::VoltageSource:
    case ComponentKind::CurrentSource:
        return {{0, -2}, {0, 2}};
    case ComponentKind::Ground:
        return {{0, 0}};
    }
    return {};
}

std::vector<std::string_view> terminalNames(InstrumentKind kind)
{
    switch (kind) {
    case InstrumentKind::Generator:
    case InstrumentKind::Multimeter:
        return {"+", "COM"};
    case InstrumentKind::Scope:
        return {"CH1", "CH2"};
    }
    return {};
}

std::vector<Point> terminalOffsets(InstrumentKind kind)
{
    switch (kind) {
    case InstrumentKind::Generator:
        return {{2, -1}, {2, 1}};
    case InstrumentKind::Scope:
        return {{-3, -1}, {-3, 1}};
    case InstrumentKind::Multimeter:
        return {{-1, 2}, {1, 2}};
    }
    return {};
}

Point place(Point offset, const Placement& placement)
{
    Point p = offset;
    if (placement.mirrored) {
        p.x = -p.x;
    }
    // A quarter turn clockwise on screen, where y grows downwards: (x, y) -> (-y, x).
    const int turns = static_cast<int>(placement.rotation);
    for (int i = 0; i < turns; ++i) {
        p = {-p.y, p.x};
    }
    return {p.x + placement.position.x, p.y + placement.position.y};
}

Conversion convert(const Scene& scene)
{
    Conversion result;
    Points points;

    // Every point that takes part in a connection.
    std::vector<std::vector<std::size_t>> partPins;
    for (const PlacedPart& part : scene.parts) {
        std::vector<std::size_t> pins;
        for (const Point p : placed(pinOffsets(part.kind), part.placement)) {
            pins.push_back(points.id(p));
        }
        partPins.push_back(std::move(pins));
    }
    std::vector<std::vector<std::size_t>> instrumentPins;
    for (const PlacedInstrument& instrument : scene.instruments) {
        std::vector<std::size_t> pins;
        for (const Point p : placed(terminalOffsets(instrument.kind), instrument.placement)) {
            pins.push_back(points.id(p));
        }
        instrumentPins.push_back(std::move(pins));
    }
    for (const Wire& wire : scene.wires) {
        points.join(points.id(wire.from), points.id(wire.to));
    }
    for (const Point junction : scene.junctions) {
        points.id(junction);
    }
    std::vector<std::size_t> labelPoints;
    for (const NetLabel& label : scene.labels) {
        labelPoints.push_back(points.id(label.position));
    }

    // Anything that lies along a wire connects to it; crossings without a junction do not, since
    // a wire's interior is not a point of its own.
    const std::size_t known = points.size();
    for (const Wire& wire : scene.wires) {
        const std::size_t end = points.id(wire.from);
        for (std::size_t id = 0; id < known; ++id) {
            if (onSegment(points.point(id), wire.from, wire.to)) {
                points.join(id, end);
            }
        }
    }

    // Labels with the same name are the same net.
    std::map<std::string, std::size_t> labelNets;
    for (std::size_t i = 0; i < scene.labels.size(); ++i) {
        const auto [it, inserted] = labelNets.try_emplace(lower(scene.labels[i].name), labelPoints[i]);
        if (!inserted) {
            points.join(labelPoints[i], it->second);
        }
    }

    // Each net: its smallest point, whether ground is on it, its label, and its component pins.
    struct Net
    {
        Point smallest;
        bool ground = false;
        std::optional<std::string> label;
        int pins = 0;
        std::string name;
    };
    std::map<std::size_t, Net> nets;
    for (std::size_t id = 0; id < points.size(); ++id) {
        const std::size_t root = points.root(id);
        const auto [it, inserted] = nets.try_emplace(root);
        Net& net = it->second;
        net.smallest = inserted ? points.point(id) : std::min(net.smallest, points.point(id));
    }
    for (std::size_t i = 0; i < scene.parts.size(); ++i) {
        for (const std::size_t pin : partPins[i]) {
            Net& net = nets[points.root(pin)];
            ++net.pins;
            net.ground = net.ground || scene.parts[i].kind == ComponentKind::Ground;
        }
    }
    for (std::size_t i = 0; i < scene.labels.size(); ++i) {
        const NetLabel& label = scene.labels[i];
        Net& net = nets[points.root(labelPoints[i])];
        if (!net.label) {
            net.label = label.name;
        } else if (lower(*net.label) != lower(label.name)) {
            result.diagnostics.push_back(
                {Severity::Error, DiagnosticCode::ConflictingLabels, {}, label.name,
                 "One net has two labels, '" + *net.label + "' and '" + label.name +
                     "'. Remove one, or separate the wires."});
        }
    }

    // Names: ground, then labels, then N1, N2, ... by smallest point.
    std::vector<Net*> unnamed;
    for (auto& [root, net] : nets) {
        if (net.ground) {
            net.name = std::string(kGroundNet);
            if (net.label && *net.label != kGroundNet) {
                result.diagnostics.push_back(
                    {Severity::Error, DiagnosticCode::ConflictingLabels, {}, *net.label,
                     "The label '" + *net.label +
                         "' is on the ground net, which is always '0'. Remove it."});
            }
        } else if (net.label) {
            net.name = *net.label;
        } else {
            unnamed.push_back(&net);
        }
    }
    std::ranges::sort(unnamed, [](const Net* a, const Net* b) { return a->smallest < b->smallest; });
    for (std::size_t i = 0; i < unnamed.size(); ++i) {
        unnamed[i]->name = "N" + std::to_string(i + 1);
    }

    for (std::size_t i = 0; i < scene.parts.size(); ++i) {
        const PlacedPart& part = scene.parts[i];
        Component component{part.kind, part.designator, {}, part.value};
        for (const std::size_t pin : partPins[i]) {
            component.nets.push_back(nets[points.root(pin)].name);
        }
        result.circuit.components.push_back(std::move(component));
    }

    for (std::size_t i = 0; i < scene.instruments.size(); ++i) {
        const PlacedInstrument& instrument = scene.instruments[i];
        const std::vector<std::string_view> names = terminalNames(instrument.kind);
        InstrumentConnections connections{instrument.kind, instrument.name, {}};
        for (std::size_t t = 0; t < instrumentPins[i].size(); ++t) {
            const Net& net = nets[points.root(instrumentPins[i][t])];
            if (net.pins == 0) {
                connections.nets.emplace_back();
                result.diagnostics.push_back(
                    {Severity::Warning, DiagnosticCode::UnconnectedTerminal, instrument.name, {},
                     std::string(names[t]) + " of " + instrumentKindName(instrument.kind) + " " +
                         instrument.name + " is not connected to the circuit."});
            } else {
                connections.nets.push_back(net.name);
            }
        }
        result.instruments.push_back(std::move(connections));
    }
    return result;
}

} // namespace proton::schematic
