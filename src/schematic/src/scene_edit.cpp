// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/scene_edit.h"

#include "spice_line.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace proton::schematic {

namespace {

// A part's symbol extent in its own frame.
Box ownBounds(ComponentKind kind)
{
    switch (kind) {
    case ComponentKind::Resistor:
    case ComponentKind::Capacitor:
    case ComponentKind::Inductor:
        return {-2, -1, 2, 1};
    case ComponentKind::VoltageSource:
    case ComponentKind::CurrentSource:
        return {-1, -2, 1, 2};
    case ComponentKind::Ground:
        return {-1, 0, 1, 2};
    }
    return {};
}

bool onWire(Point p, const Wire& wire)
{
    const long long cross = static_cast<long long>(wire.to.x - wire.from.x) * (p.y - wire.from.y) -
                            static_cast<long long>(wire.to.y - wire.from.y) * (p.x - wire.from.x);
    return cross == 0 && p.x >= std::min(wire.from.x, wire.to.x) &&
           p.x <= std::max(wire.from.x, wire.to.x) && p.y >= std::min(wire.from.y, wire.to.y) &&
           p.y <= std::max(wire.from.y, wire.to.y);
}

std::vector<Point> pinsOf(const PlacedPart& part)
{
    std::vector<Point> pins;
    for (const Point offset : pinOffsets(part.kind)) {
        pins.push_back(place(offset, part.placement));
    }
    return pins;
}

// Moves the wire ends that sat on `before[i]` to `after[i]`.
void followPins(Scene& scene, const std::vector<Point>& before, const std::vector<Point>& after)
{
    for (Wire& wire : scene.wires) {
        for (Point* end : {&wire.from, &wire.to}) {
            for (std::size_t i = 0; i < before.size(); ++i) {
                if (*end == before[i]) {
                    *end = after[i];
                    break;
                }
            }
        }
    }
}

template <typename T>
void eraseIndices(std::vector<T>& items, const std::vector<std::size_t>& indices)
{
    const std::set<std::size_t> drop(indices.begin(), indices.end());
    std::vector<T> kept;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!drop.contains(i)) {
            kept.push_back(std::move(items[i]));
        }
    }
    items = std::move(kept);
}

} // namespace

Box bounds(const PlacedPart& part)
{
    const Box own = ownBounds(part.kind);
    const Point a = place({own.left, own.top}, part.placement);
    const Point b = place({own.right, own.bottom}, part.placement);
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

std::optional<std::size_t> partAt(const Scene& scene, Point p)
{
    for (std::size_t i = scene.parts.size(); i-- > 0;) {
        if (bounds(scene.parts[i]).contains(p)) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> wireAt(const Scene& scene, Point p)
{
    for (std::size_t i = scene.wires.size(); i-- > 0;) {
        if (onWire(p, scene.wires[i])) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> labelAt(const Scene& scene, Point p)
{
    for (std::size_t i = scene.labels.size(); i-- > 0;) {
        if (scene.labels[i].position == p) {
            return i;
        }
    }
    return std::nullopt;
}

std::string nextDesignator(const Scene& scene, ComponentKind kind)
{
    if (kind == ComponentKind::Ground) {
        return {};
    }
    const char letter = detail::spiceLetter(kind);
    int highest = 0;
    for (const PlacedPart& part : scene.parts) {
        const std::string& name = part.designator;
        if (name.size() < 2 || detail::toUpper(name.front()) != letter) {
            continue;
        }
        const std::string digits = name.substr(1);
        const auto isDigit = [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        };
        if (digits.size() < 9 && std::ranges::all_of(digits, isDigit)) {
            highest = std::max(highest, std::stoi(digits));
        }
    }
    return std::string(1, letter) + std::to_string(highest + 1);
}

std::string defaultValue(ComponentKind kind)
{
    switch (kind) {
    case ComponentKind::Resistor:
        return "1k";
    case ComponentKind::Capacitor:
        return "100n";
    case ComponentKind::Inductor:
        return "1m";
    case ComponentKind::VoltageSource:
        return "5";
    case ComponentKind::CurrentSource:
        return "1m";
    case ComponentKind::Ground:
        return {};
    }
    return {};
}

std::vector<Wire> route(Point from, Point to)
{
    if (from == to) {
        return {};
    }
    if (from.x == to.x || from.y == to.y) {
        return {{from, to}};
    }
    const Point corner{to.x, from.y};
    return {{from, corner}, {corner, to}};
}

std::vector<Point> junctionDots(const Scene& scene)
{
    std::map<Point, int> connections;
    for (const Wire& wire : scene.wires) {
        ++connections[wire.from];
        ++connections[wire.to];
    }
    for (const PlacedPart& part : scene.parts) {
        for (const Point pin : pinsOf(part)) {
            ++connections[pin];
        }
    }
    // A wire passing through a point already counted connects to it twice, once on each side.
    std::map<Point, int> through;
    for (const auto& [point, count] : connections) {
        for (const Wire& wire : scene.wires) {
            if (point != wire.from && point != wire.to && onWire(point, wire)) {
                through[point] += 2;
            }
        }
    }
    std::set<Point> dots(scene.junctions.begin(), scene.junctions.end());
    for (const auto& [point, count] : connections) {
        if (count + through[point] >= 3) {
            dots.insert(point);
        }
    }
    return {dots.begin(), dots.end()};
}

void movePart(Scene& scene, std::size_t index, Point position)
{
    if (index >= scene.parts.size()) {
        return;
    }
    PlacedPart& part = scene.parts[index];
    const std::vector<Point> before = pinsOf(part);
    part.placement.position = position;
    followPins(scene, before, pinsOf(part));
}

void rotatePart(Scene& scene, std::size_t index)
{
    if (index >= scene.parts.size()) {
        return;
    }
    PlacedPart& part = scene.parts[index];
    const std::vector<Point> before = pinsOf(part);
    part.placement.rotation =
        static_cast<Rotation>((static_cast<int>(part.placement.rotation) + 1) % 4);
    followPins(scene, before, pinsOf(part));
}

void remove(Scene& scene, const std::vector<std::size_t>& parts,
            const std::vector<std::size_t>& wires, const std::vector<std::size_t>& labels)
{
    eraseIndices(scene.parts, parts);
    eraseIndices(scene.wires, wires);
    eraseIndices(scene.labels, labels);
}

} // namespace proton::schematic
