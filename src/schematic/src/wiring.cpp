// SPDX-License-Identifier: AGPL-3.0-only
// Wiring as a desktop circuit simulator does it (PLAN.md M4.12): the routing behind
// scene_edit.h's moveSelection(), dragSegment(), dragWireEnd() and the rest.
#include "proton/schematic/scene_edit.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <utility>

namespace proton::schematic {

namespace {

Point plus(Point a, Point b)
{
    return {a.x + b.x, a.y + b.y};
}

Point minus(Point a, Point b)
{
    return {a.x - b.x, a.y - b.y};
}

bool horizontal(const Wire& wire)
{
    return wire.from.y == wire.to.y && wire.from.x != wire.to.x;
}

bool vertical(const Wire& wire)
{
    return wire.from.x == wire.to.x && wire.from.y != wire.to.y;
}

// Whether `p` lies on `wire`, ends included.
bool onWire(Point p, const Wire& wire)
{
    const long long cross = static_cast<long long>(wire.to.x - wire.from.x) * (p.y - wire.from.y) -
                            static_cast<long long>(wire.to.y - wire.from.y) * (p.x - wire.from.x);
    return cross == 0 && p.x >= std::min(wire.from.x, wire.to.x) &&
           p.x <= std::max(wire.from.x, wire.to.x) && p.y >= std::min(wire.from.y, wire.to.y) &&
           p.y <= std::max(wire.from.y, wire.to.y);
}

// Whether `p` lies on `wire` between its ends.
bool inside(Point p, const Wire& wire)
{
    return p != wire.from && p != wire.to && onWire(p, wire);
}

std::vector<Point> placedPoints(const std::vector<Point>& offsets, const Placement& placement)
{
    std::vector<Point> points;
    for (const Point offset : offsets) {
        points.push_back(place(offset, placement));
    }
    return points;
}

std::vector<Point> pinsOf(const PlacedPart& part)
{
    return placedPoints(pinOffsets(part.kind), part.placement);
}

std::vector<Point> terminalsOf(const PlacedInstrument& instrument)
{
    return placedPoints(terminalOffsets(instrument.kind), instrument.placement);
}

// Every pin and terminal of the scene.
std::vector<Point> allPins(const Scene& scene)
{
    std::vector<Point> pins;
    for (const PlacedPart& part : scene.parts) {
        for (const Point p : pinsOf(part)) {
            pins.push_back(p);
        }
    }
    for (const PlacedInstrument& instrument : scene.instruments) {
        for (const Point p : terminalsOf(instrument)) {
            pins.push_back(p);
        }
    }
    return pins;
}

bool labelOrJunctionAt(const Scene& scene, Point p)
{
    return std::ranges::find(scene.junctions, p) != scene.junctions.end() ||
           std::ranges::any_of(scene.labels,
                               [p](const NetLabel& label) { return label.position == p; });
}

// Whether a wire other than those in `except` runs through `p` between its ends.
bool wireThrough(const Scene& scene, Point p, const std::set<std::size_t>& except = {})
{
    for (std::size_t i = 0; i < scene.wires.size(); ++i) {
        if (!except.contains(i) && inside(p, scene.wires[i])) {
            return true;
        }
    }
    return false;
}

// The wires with an end at `p`.
std::vector<std::size_t> wiresEndingAt(const Scene& scene, Point p)
{
    std::vector<std::size_t> found;
    for (std::size_t i = 0; i < scene.wires.size(); ++i) {
        if (scene.wires[i].from == p || scene.wires[i].to == p) {
            found.push_back(i);
        }
    }
    return found;
}

Point otherEnd(const Wire& wire, Point end)
{
    return wire.from == end ? wire.to : wire.from;
}

// Splits every wire at the points along its middle that it connects to (pins, other wires' ends,
// junctions, labels), so a chain of wires stops at them. Returns the pieces of each wire: the
// first keeps its index, the others go at the end.
std::vector<std::vector<std::size_t>> splitAtConnections(Scene& scene)
{
    std::set<Point> points;
    for (const Point p : allPins(scene)) {
        points.insert(p);
    }
    for (const Wire& wire : scene.wires) {
        points.insert(wire.from);
        points.insert(wire.to);
    }
    points.insert(scene.junctions.begin(), scene.junctions.end());
    for (const NetLabel& label : scene.labels) {
        points.insert(label.position);
    }
    const std::size_t count = scene.wires.size();
    std::vector<std::vector<std::size_t>> pieces(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Wire wire = scene.wires[i];
        std::vector<Point> cuts;
        for (const Point p : points) {
            if (inside(p, wire)) {
                cuts.push_back(p);
            }
        }
        // In order from `from`.
        std::ranges::sort(cuts, [&wire](Point a, Point b) {
            return std::abs(a.x - wire.from.x) + std::abs(a.y - wire.from.y) <
                   std::abs(b.x - wire.from.x) + std::abs(b.y - wire.from.y);
        });
        pieces[i] = {i};
        Point start = wire.from;
        for (std::size_t k = 0; k < cuts.size(); ++k) {
            if (k == 0) {
                scene.wires[i] = {start, cuts[k]};
            } else {
                pieces[i].push_back(scene.wires.size());
                scene.wires.push_back({start, cuts[k]});
            }
            start = cuts[k];
        }
        if (!cuts.empty()) {
            pieces[i].push_back(scene.wires.size());
            scene.wires.push_back({start, wire.to});
        }
    }
    return pieces;
}

// A point where a chain of wires just bends: two wire ends and nothing else.
bool plainBend(const Scene& scene, Point p)
{
    return wiresEndingAt(scene, p).size() == 2 && !pinAt(scene, p) &&
           !labelOrJunctionAt(scene, p) && !wireThrough(scene, p);
}

struct Chain
{
    std::vector<std::size_t> wires;
    Point far; // where it first meets something other than a plain bend
};

// The chain of wires from `start` through wire `first` up to its far end, the wires in `used`
// not taken; adds its wires to `used`.
Chain chainFrom(const Scene& scene, Point start, std::size_t first, std::set<std::size_t>& used,
                const std::function<bool(Point)>& stopAt)
{
    Chain chain;
    std::size_t wire = first;
    Point at = start;
    for (;;) {
        used.insert(wire);
        chain.wires.push_back(wire);
        at = otherEnd(scene.wires[wire], at);
        if (stopAt(at) || !plainBend(scene, at)) {
            break;
        }
        const std::vector<std::size_t> ends = wiresEndingAt(scene, at);
        const std::size_t next = ends[0] == wire ? ends[1] : ends[0];
        if (used.contains(next)) {
            break; // a loop of bends
        }
        wire = next;
    }
    chain.far = at;
    return chain;
}

// ---- Routing --------------------------------------------------------------------------------

constexpr int kBendCost = 3;   // a bend costs as much as this many grid steps
constexpr int kSymbolCost = 1000; // extra for each step inside a symbol's box: a last resort
constexpr int kNearCost = 1;   // extra for each step on its edge or a unit round it, where its
                               // name and value are drawn
constexpr std::array<Point, 4> kSteps = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

// The grid search behind routeAround() within `margin` of the two ends: A*, the state a point
// and the direction it was entered in, so bends can be costed.
std::optional<std::vector<Wire>> search(const Scene& scene, Point from, Point to, int margin)
{
    const int left = std::min(from.x, to.x) - margin;
    const int top = std::min(from.y, to.y) - margin;
    const int width = std::abs(from.x - to.x) + 2 * margin + 1;
    const int height = std::abs(from.y - to.y) + 2 * margin + 1;
    const auto cell = [&](Point p) { return (p.y - top) * width + (p.x - left); };
    const auto inGrid = [&](Point p) {
        return p.x >= left && p.x < left + width && p.y >= top && p.y < top + height;
    };
    const std::size_t cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    // What a route may not touch, may not run along, and would rather not cross.
    std::vector<char> blocked(cells, 0);
    std::vector<char> alongX(cells, 0); // on a horizontal wire
    std::vector<char> alongY(cells, 0); // on a vertical wire
    std::vector<char> symbol(cells, 0); // 2 inside a box, 1 on its edge or round it
    const auto mark = [&](std::vector<char>& grid, Point p) {
        if (inGrid(p)) {
            grid[static_cast<std::size_t>(cell(p))] = 1;
        }
    };
    for (const Point p : allPins(scene)) {
        mark(blocked, p);
    }
    for (const Wire& wire : scene.wires) {
        mark(blocked, wire.from);
        mark(blocked, wire.to);
        if (horizontal(wire)) {
            for (int x = std::min(wire.from.x, wire.to.x); x <= std::max(wire.from.x, wire.to.x);
                 ++x) {
                mark(alongX, {x, wire.from.y});
            }
        } else if (vertical(wire)) {
            for (int y = std::min(wire.from.y, wire.to.y); y <= std::max(wire.from.y, wire.to.y);
                 ++y) {
                mark(alongY, {wire.from.x, y});
            }
        }
    }
    for (const Point junction : scene.junctions) {
        mark(blocked, junction);
    }
    for (const NetLabel& label : scene.labels) {
        mark(blocked, label.position);
    }
    const auto markBox = [&](const Box& box) {
        for (int y = std::max(box.top - 1, top); y <= std::min(box.bottom + 1, top + height - 1);
             ++y) {
            for (int x = std::max(box.left - 1, left);
                 x <= std::min(box.right + 1, left + width - 1); ++x) {
                const bool interior =
                    x > box.left && x < box.right && y > box.top && y < box.bottom;
                char& c = symbol[static_cast<std::size_t>(cell({x, y}))];
                c = std::max(c, static_cast<char>(interior ? 2 : 1));
            }
        }
    };
    for (const PlacedPart& part : scene.parts) {
        markBox(bounds(part));
    }
    for (const PlacedInstrument& instrument : scene.instruments) {
        markBox(bounds(instrument));
    }
    const auto at = [&](const std::vector<char>& grid, Point p) {
        return grid[static_cast<std::size_t>(cell(p))] != 0;
    };

    // States: cell * 4 + the direction of the step into it.
    const std::size_t states = cells * 4;
    std::vector<int> cost(states, INT_MAX);
    std::vector<long long> parent(states, -1); // -2: entered straight from `from`
    using Entry = std::pair<int, std::size_t>; // estimate, state
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    const auto estimate = [&](Point p) { return std::abs(p.x - to.x) + std::abs(p.y - to.y); };
    const auto pointOf = [&](std::size_t state) {
        const int c = static_cast<int>(state / 4);
        return Point{left + c % width, top + c / width};
    };
    // A step from `p` in direction `d`, entered in direction `entered` (-1 at `from`).
    const auto stepCost = [&](Point p, int entered, int d) -> std::optional<int> {
        const Point q = plus(p, kSteps[static_cast<std::size_t>(d)]);
        if (!inGrid(q) || q == from || (q != to && at(blocked, q))) {
            return std::nullopt;
        }
        const bool sideways = d < 2;
        if ((sideways && at(alongX, p) && at(alongX, q)) ||
            (!sideways && at(alongY, p) && at(alongY, q))) {
            return std::nullopt; // along a wire
        }
        int extra = 0;
        if (entered >= 0 && entered != d) {
            if (at(alongX, p) || at(alongY, p)) {
                return std::nullopt; // a bend on a wire would join it
            }
            extra += kBendCost;
        }
        if (q != to) {
            const char near = symbol[static_cast<std::size_t>(cell(q))];
            extra += near == 2 ? kSymbolCost : near == 1 ? kNearCost : 0;
        }
        return 1 + extra;
    };

    for (int d = 0; d < 4; ++d) {
        if (const std::optional<int> c = stepCost(from, -1, d)) {
            const Point q = plus(from, kSteps[static_cast<std::size_t>(d)]);
            const std::size_t s = static_cast<std::size_t>(cell(q)) * 4 + static_cast<std::size_t>(d);
            if (*c < cost[s]) {
                cost[s] = *c;
                parent[s] = -2;
                open.emplace(*c + estimate(q), s);
            }
        }
    }
    std::optional<std::size_t> goal;
    while (!open.empty()) {
        const auto [f, s] = open.top();
        open.pop();
        const Point p = pointOf(s);
        if (f - estimate(p) > cost[s]) {
            continue; // a stale entry
        }
        if (p == to) {
            goal = s;
            break;
        }
        const int entered = static_cast<int>(s % 4);
        for (int d = 0; d < 4; ++d) {
            if ((d ^ 1) == entered) {
                continue; // straight back
            }
            if (const std::optional<int> c = stepCost(p, entered, d)) {
                const Point q = plus(p, kSteps[static_cast<std::size_t>(d)]);
                const std::size_t next =
                    static_cast<std::size_t>(cell(q)) * 4 + static_cast<std::size_t>(d);
                if (cost[s] + *c < cost[next]) {
                    cost[next] = cost[s] + *c;
                    parent[next] = static_cast<long long>(s);
                    open.emplace(cost[next] + estimate(q), next);
                }
            }
        }
    }
    if (!goal) {
        return std::nullopt;
    }

    // Back from the goal, keeping the corners.
    std::vector<Point> corners = {to};
    std::size_t s = *goal;
    int direction = static_cast<int>(s % 4);
    while (parent[s] >= 0) {
        s = static_cast<std::size_t>(parent[s]);
        if (static_cast<int>(s % 4) != direction) {
            corners.push_back(pointOf(s));
            direction = static_cast<int>(s % 4);
        }
    }
    corners.push_back(from);
    std::ranges::reverse(corners);
    std::vector<Wire> wires;
    for (std::size_t i = 1; i < corners.size(); ++i) {
        wires.push_back({corners[i - 1], corners[i]});
    }
    return wires;
}

// ---- Rerouting after a move -----------------------------------------------------------------

// The pins and terminals of the parts and instruments `selection` does not hold.
std::set<Point> pinsStaying(const Scene& scene, const Selection& selection)
{
    std::set<Point> pins;
    for (std::size_t i = 0; i < scene.parts.size(); ++i) {
        if (std::ranges::find(selection.parts, i) == selection.parts.end()) {
            for (const Point p : pinsOf(scene.parts[i])) {
                pins.insert(p);
            }
        }
    }
    for (std::size_t i = 0; i < scene.instruments.size(); ++i) {
        if (std::ranges::find(selection.instruments, i) == selection.instruments.end()) {
            for (const Point p : terminalsOf(scene.instruments[i])) {
                pins.insert(p);
            }
        }
    }
    return pins;
}

// Routes again what `before` had wired to points that moved (`moves`, old to new), in `scene`:
// `before` with the items moved and the wires of `moved` (indices in both) moved whole; `staying`
// are the pins that did not move. Returns where the wires of `moved` are now.
std::vector<std::size_t> rewire(Scene& scene, const Scene& before,
                                const std::map<Point, Point>& moves,
                                const std::set<std::size_t>& moved, const std::set<Point>& staying)
{
    std::set<std::size_t> used = moved;
    const auto moving = [&](Point p) { return moves.contains(p); };
    std::vector<Wire> added;                  // chains moved whole
    std::vector<std::pair<Point, Point>> routes; // to route, in order
    for (const auto& [from, to] : moves) {
        for (const std::size_t wire : wiresEndingAt(before, from)) {
            if (used.contains(wire)) {
                continue;
            }
            const Chain chain = chainFrom(before, from, wire, used, moving);
            const auto far = moves.find(chain.far);
            if (far != moves.end() && minus(far->second, far->first) == minus(to, from)) {
                for (const std::size_t i : chain.wires) {
                    const Point offset = minus(to, from);
                    added.push_back({plus(before.wires[i].from, offset),
                                     plus(before.wires[i].to, offset)});
                }
            } else {
                routes.emplace_back(to, far != moves.end() ? far->second : chain.far);
            }
        }
    }
    // Whatever stays where a pin was keeps its connection to it.
    for (const auto& [from, to] : moves) {
        const bool pinStays = staying.contains(from);
        const bool wireStays = wireThrough(before, from, used);
        if (from != to && (pinStays || wireStays || labelOrJunctionAt(scene, from))) {
            routes.emplace_back(to, from);
        }
    }

    std::vector<Wire> wires;
    std::vector<std::size_t> where;
    for (std::size_t i = 0; i < scene.wires.size(); ++i) {
        if (moved.contains(i)) {
            where.push_back(wires.size());
            wires.push_back(scene.wires[i]);
        } else if (!used.contains(i)) {
            wires.push_back(scene.wires[i]);
        }
    }
    wires.insert(wires.end(), added.begin(), added.end());
    scene.wires = std::move(wires);
    for (const auto& [a, b] : routes) {
        for (const Wire& wire : routeAround(scene, a, b)) {
            scene.wires.push_back(wire);
        }
    }
    return where;
}

// The pins and terminals of a scene in order, each with the group of its net: the first pin of
// a net names the group.
std::vector<std::size_t> netGroups(const Scene& scene)
{
    std::map<Point, std::size_t> ids;
    std::vector<std::size_t> parent;
    const auto id = [&](Point p) {
        const auto [it, inserted] = ids.try_emplace(p, parent.size());
        if (inserted) {
            parent.push_back(parent.size());
        }
        return it->second;
    };
    const std::function<std::size_t(std::size_t)> root = [&](std::size_t i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    };
    const auto join = [&](std::size_t a, std::size_t b) { parent[root(a)] = root(b); };

    std::vector<std::size_t> pins;
    std::optional<std::size_t> ground;
    for (const PlacedPart& part : scene.parts) {
        for (const Point p : pinsOf(part)) {
            pins.push_back(id(p));
            if (part.kind == ComponentKind::Ground) {
                // Every ground symbol is node 0.
                if (ground) {
                    join(pins.back(), *ground);
                }
                ground = pins.back();
            }
        }
    }
    for (const PlacedInstrument& instrument : scene.instruments) {
        for (const Point p : terminalsOf(instrument)) {
            pins.push_back(id(p));
        }
    }
    for (const Wire& wire : scene.wires) {
        join(id(wire.from), id(wire.to));
    }
    for (const Point junction : scene.junctions) {
        id(junction);
    }
    std::map<std::string, std::size_t> labels;
    for (const NetLabel& label : scene.labels) {
        std::string name = label.name;
        for (char& c : name) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const std::size_t at = id(label.position);
        const auto [it, inserted] = labels.try_emplace(name, at);
        if (!inserted) {
            join(at, it->second);
        }
    }
    for (const Wire& wire : scene.wires) {
        const std::size_t end = id(wire.from); // already there: ids does not change below
        for (const auto& [p, i] : ids) {
            if (onWire(p, wire)) {
                join(i, end);
            }
        }
    }
    std::map<std::size_t, std::size_t> group;
    std::vector<std::size_t> groups;
    for (std::size_t k = 0; k < pins.size(); ++k) {
        groups.push_back(group.try_emplace(root(pins[k]), k).first->second);
    }
    return groups;
}

} // namespace

bool pinAt(const Scene& scene, Point p)
{
    const std::vector<Point> pins = allPins(scene);
    return std::ranges::find(pins, p) != pins.end();
}

std::optional<std::size_t> wireEndAt(const Scene& scene, Point p)
{
    const std::vector<std::size_t> ends = wiresEndingAt(scene, p);
    if (ends.size() != 1 || labelOrJunctionAt(scene, p) || wireThrough(scene, p)) {
        return std::nullopt;
    }
    return ends.front();
}

bool crossesSymbol(const Scene& scene, const Wire& wire)
{
    // The points of the wire strictly inside the box; a slanted wire, from an older drawing, by
    // the box round it.
    const auto crosses = [&wire](const Box& box) {
        const int left = std::max(std::min(wire.from.x, wire.to.x), box.left + 1);
        const int right = std::min(std::max(wire.from.x, wire.to.x), box.right - 1);
        const int top = std::max(std::min(wire.from.y, wire.to.y), box.top + 1);
        const int bottom = std::min(std::max(wire.from.y, wire.to.y), box.bottom - 1);
        return left <= right && top <= bottom;
    };
    return std::ranges::any_of(scene.parts,
                               [&](const PlacedPart& part) { return crosses(bounds(part)); }) ||
           std::ranges::any_of(scene.instruments, [&](const PlacedInstrument& instrument) {
               return crosses(bounds(instrument));
           });
}

std::vector<Wire> wireToolRoute(const Scene& scene, Point from, Point to)
{
    std::vector<Wire> wires = route(from, to);
    if (std::ranges::any_of(wires, [&](const Wire& wire) { return crossesSymbol(scene, wire); })) {
        return routeAround(scene, from, to);
    }
    return wires;
}

std::vector<Wire> routeAround(const Scene& scene, Point from, Point to)
{
    if (from == to) {
        return {};
    }
    for (const int margin : {6, 24}) {
        if (std::optional<std::vector<Wire>> wires = search(scene, from, to, margin)) {
            return std::move(*wires);
        }
    }
    return route(from, to);
}

Selection moveSelection(Scene& scene, const Selection& selection, Point offset)
{
    if (offset == Point{0, 0}) {
        return selection;
    }
    // A selected wire moves with all its pieces.
    const std::vector<std::vector<std::size_t>> pieces = splitAtConnections(scene);
    std::vector<std::size_t> selectedWires;
    for (const std::size_t i : selection.wires) {
        if (i < pieces.size()) {
            selectedWires.insert(selectedWires.end(), pieces[i].begin(), pieces[i].end());
        }
    }
    const Scene before = scene;
    std::map<Point, Point> moves;
    const auto moveAll = [&](const std::vector<Point>& points) {
        for (const Point p : points) {
            moves.emplace(p, plus(p, offset));
        }
    };
    for (const std::size_t i : selection.parts) {
        if (i < scene.parts.size()) {
            moveAll(pinsOf(scene.parts[i]));
            scene.parts[i].placement.position = plus(scene.parts[i].placement.position, offset);
        }
    }
    for (const std::size_t i : selection.instruments) {
        if (i < scene.instruments.size()) {
            moveAll(terminalsOf(scene.instruments[i]));
            scene.instruments[i].placement.position =
                plus(scene.instruments[i].placement.position, offset);
        }
    }
    std::set<std::size_t> wires;
    for (const std::size_t i : selectedWires) {
        if (i < scene.wires.size()) {
            wires.insert(i);
            moveAll({scene.wires[i].from, scene.wires[i].to});
            scene.wires[i] = {plus(scene.wires[i].from, offset), plus(scene.wires[i].to, offset)};
        }
    }
    for (const std::size_t i : selection.labels) {
        if (i < scene.labels.size()) {
            moveAll({scene.labels[i].position});
            scene.labels[i].position = plus(scene.labels[i].position, offset);
        }
    }
    for (const std::size_t i : selection.junctions) {
        if (i < scene.junctions.size()) {
            moveAll({scene.junctions[i]});
            scene.junctions[i] = plus(scene.junctions[i], offset);
        }
    }
    Selection result = selection;
    result.wires = rewire(scene, before, moves, wires, pinsStaying(scene, selection));
    return result;
}

namespace {

// Turns or mirrors the selected parts and instruments with `change`, then routes their wires.
void reshape(Scene& scene, const Selection& selection,
             const std::function<void(Scene&, bool, std::size_t)>& change)
{
    splitAtConnections(scene);
    const Scene before = scene;
    std::map<Point, Point> moves;
    const auto record = [&](const std::vector<Point>& was, const std::vector<Point>& now) {
        for (std::size_t k = 0; k < was.size() && k < now.size(); ++k) {
            if (was[k] != now[k]) {
                moves.emplace(was[k], now[k]);
            }
        }
    };
    for (const std::size_t i : selection.parts) {
        if (i < scene.parts.size()) {
            // On a scene of its own, so the low-level edit's straight wire following stays out.
            Scene single;
            single.parts = {scene.parts[i]};
            change(single, true, 0);
            record(pinsOf(scene.parts[i]), pinsOf(single.parts[0]));
            scene.parts[i] = single.parts[0];
        }
    }
    for (const std::size_t i : selection.instruments) {
        if (i < scene.instruments.size()) {
            Scene single;
            single.instruments = {scene.instruments[i]};
            change(single, false, 0);
            record(terminalsOf(scene.instruments[i]), terminalsOf(single.instruments[0]));
            scene.instruments[i] = single.instruments[0];
        }
    }
    rewire(scene, before, moves, {}, pinsStaying(scene, selection));
}

} // namespace

void rotateSelection(Scene& scene, const Selection& selection)
{
    reshape(scene, selection, [](Scene& single, bool part, std::size_t i) {
        part ? rotatePart(single, i) : rotateInstrument(single, i);
    });
}

void mirrorSelection(Scene& scene, const Selection& selection)
{
    reshape(scene, selection, [](Scene& single, bool part, std::size_t i) {
        part ? mirrorPart(single, i) : mirrorInstrument(single, i);
    });
}

void dragSegment(Scene& scene, std::size_t index, Point offset)
{
    if (index >= scene.wires.size()) {
        return;
    }
    const Wire wire = scene.wires[index];
    const Point d = horizontal(wire) ? Point{0, offset.y}
                    : vertical(wire) ? Point{offset.x, 0}
                                     : offset;
    if (d == Point{0, 0} || wire.from == wire.to) {
        return;
    }
    const Scene before = scene;
    std::vector<Wire> stubs;
    for (const Point end : {wire.from, wire.to}) {
        std::vector<std::size_t> others = wiresEndingAt(before, end);
        std::erase(others, index);
        const bool more = pinAt(before, end) || labelOrJunctionAt(before, end) ||
                          wireThrough(before, end, {index});
        // A single wire along the drag direction stretches; anything else gets a stub.
        if (others.size() == 1 && !more) {
            Wire& other = scene.wires[others[0]];
            const bool along = d.y == 0   ? horizontal(other)
                               : d.x == 0 ? vertical(other)
                                          : false; // a slanted drag stretches nothing
            if (along) {
                (other.from == end ? other.from : other.to) = plus(end, d);
                continue;
            }
        }
        if (!others.empty() || more) {
            for (const Wire& stub : route(end, plus(end, d))) {
                stubs.push_back(stub);
            }
        }
    }
    // Pins, wire ends, labels and junctions along its middle were joined to it: stubs keep them.
    std::set<Point> along;
    for (const Point p : allPins(before)) {
        along.insert(p);
    }
    for (std::size_t i = 0; i < before.wires.size(); ++i) {
        if (i != index) {
            along.insert(before.wires[i].from);
            along.insert(before.wires[i].to);
        }
    }
    along.insert(before.junctions.begin(), before.junctions.end());
    for (const NetLabel& label : before.labels) {
        along.insert(label.position);
    }
    for (const Point p : along) {
        if (inside(p, wire)) {
            for (const Wire& stub : route(p, plus(p, d))) {
                stubs.push_back(stub);
            }
        }
    }
    scene.wires[index] = {plus(wire.from, d), plus(wire.to, d)};
    scene.wires.insert(scene.wires.end(), stubs.begin(), stubs.end());
}

void dragWireEnd(Scene& scene, std::size_t index, Point end, Point to)
{
    if (index >= scene.wires.size() || end == to ||
        (scene.wires[index].from != end && scene.wires[index].to != end)) {
        return;
    }
    // From the piece of the wire that has the end.
    const std::vector<std::vector<std::size_t>> pieces = splitAtConnections(scene);
    for (const std::size_t piece : pieces[index]) {
        if (scene.wires[piece].from == end || scene.wires[piece].to == end) {
            index = piece;
            break;
        }
    }
    std::set<std::size_t> used;
    const Chain chain = chainFrom(scene, end, index, used, [](Point) { return false; });
    std::vector<Wire> kept;
    for (std::size_t i = 0; i < scene.wires.size(); ++i) {
        if (!used.contains(i)) {
            kept.push_back(scene.wires[i]);
        }
    }
    scene.wires = std::move(kept);
    if (chain.far == to) {
        return; // dragged onto its far end: the chain is gone
    }
    tapWire(scene, to);
    for (const Wire& wire : routeAround(scene, to, chain.far)) {
        scene.wires.push_back(wire);
    }
}

bool tapWire(Scene& scene, Point p)
{
    for (std::size_t i = 0; i < scene.wires.size(); ++i) {
        const Wire wire = scene.wires[i];
        if (inside(p, wire)) {
            scene.wires[i] = {wire.from, p};
            scene.wires.push_back({p, wire.to});
            return true;
        }
    }
    return false;
}

void tidyWires(Scene& scene)
{
    std::erase_if(scene.wires, [](const Wire& wire) { return wire.from == wire.to; });
    // Drawn twice: the later goes.
    std::set<std::pair<Point, Point>> seen;
    std::erase_if(scene.wires, [&seen](const Wire& wire) {
        const std::pair<Point, Point> key = std::minmax(wire.from, wire.to);
        return !seen.insert(key).second;
    });
    // Straight continuations with nothing else where they meet, until there are none.
    for (bool merged = true; merged;) {
        merged = false;
        std::map<Point, std::vector<std::size_t>> ends;
        for (std::size_t i = 0; i < scene.wires.size(); ++i) {
            ends[scene.wires[i].from].push_back(i);
            ends[scene.wires[i].to].push_back(i);
        }
        for (const auto& [p, wires] : ends) {
            if (wires.size() != 2) {
                continue;
            }
            const Wire a = scene.wires[wires[0]];
            const Wire b = scene.wires[wires[1]];
            const bool straight = (horizontal(a) && horizontal(b) && a.from.y == b.from.y) ||
                                  (vertical(a) && vertical(b) && a.from.x == b.from.x);
            const Point farA = otherEnd(a, p);
            const Point farB = otherEnd(b, p);
            // They continue each other when p lies between their far ends.
            if (!straight || !inside(p, {farA, farB}) || pinAt(scene, p) ||
                labelOrJunctionAt(scene, p) || wireThrough(scene, p)) {
                continue;
            }
            scene.wires[wires[0]] = {farA, farB};
            scene.wires.erase(scene.wires.begin() + static_cast<std::ptrdiff_t>(wires[1]));
            merged = true;
            break;
        }
    }
}

bool sameNets(const Scene& a, const Scene& b)
{
    const auto kinds = [](const Scene& scene) {
        std::vector<int> result;
        for (const PlacedPart& part : scene.parts) {
            result.push_back(static_cast<int>(part.kind));
        }
        result.push_back(-1);
        for (const PlacedInstrument& instrument : scene.instruments) {
            result.push_back(static_cast<int>(instrument.kind));
        }
        return result;
    };
    return kinds(a) == kinds(b) && netGroups(a) == netGroups(b);
}

} // namespace proton::schematic
