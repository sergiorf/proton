// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/netlist.h"

#include "proton/core/spice_number.h"
#include "spice_line.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace proton::schematic {

namespace {

using core::formatSpiceNumber;
using core::parseSpiceNumber;
using detail::isBlankOrComment;
using detail::isSpace;
using detail::lower;
using detail::parseElementLine;
using detail::toLower;

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

// Natural order ignoring case: runs of digits compare by value, so R2 < R10. Names equal that way
// ("R01", "r1") fall back to plain comparison, which keeps the order total.
bool naturalLess(std::string_view a, std::string_view b)
{
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (isDigit(a[i]) && isDigit(b[j])) {
            const std::size_t startA = i;
            const std::size_t startB = j;
            while (i < a.size() && isDigit(a[i])) {
                ++i;
            }
            while (j < b.size() && isDigit(b[j])) {
                ++j;
            }
            std::string_view digitsA = a.substr(startA, i - startA);
            std::string_view digitsB = b.substr(startB, j - startB);
            digitsA.remove_prefix(std::min(digitsA.find_first_not_of('0'), digitsA.size()));
            digitsB.remove_prefix(std::min(digitsB.find_first_not_of('0'), digitsB.size()));
            if (digitsA.size() != digitsB.size()) {
                return digitsA.size() < digitsB.size();
            }
            if (digitsA != digitsB) {
                return digitsA < digitsB;
            }
            continue;
        }
        const char ca = toLower(a[i]);
        const char cb = toLower(b[j]);
        if (ca != cb) {
            return ca < cb;
        }
        ++i;
        ++j;
    }
    if (i < a.size() || j < b.size()) {
        return j < b.size(); // the shorter one first
    }
    return a < b;
}

std::string_view trimRight(std::string_view text)
{
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::string normalisedValue(const std::string& value)
{
    const std::optional<double> number = parseSpiceNumber(value);
    return number ? formatSpiceNumber(*number) : value;
}

class Writer
{
public:
    explicit Writer(const Circuit& circuit)
    {
        groundNets_.insert(std::string(kGroundNet));
        for (const Component& component : circuit.components) {
            if (component.kind == ComponentKind::Ground && component.nets.size() == 1) {
                groundNets_.insert(lower(component.nets.front()));
            }
        }
    }

    std::string_view node(std::string_view net) const
    {
        return groundNets_.count(lower(net)) != 0 ? kGroundNet : net;
    }

    void line(std::string_view text)
    {
        text_ += text;
        text_ += '\n';
    }

    void component(const Component& component)
    {
        if (component.kind == ComponentKind::Ground) {
            return;
        }
        std::string text = component.designator;
        for (const std::string& net : component.nets) {
            text += ' ';
            text += node(net);
        }
        text += ' ';
        if (component.kind == ComponentKind::VoltageSource ||
            component.kind == ComponentKind::CurrentSource) {
            text += "DC ";
        }
        text += normalisedValue(component.value);
        line(text);
    }

    void fragment(const Fragment& fragment)
    {
        line("* " + fragment.name);
        for (const std::string& raw : fragment.lines) {
            if (isBlankOrComment(raw)) {
                if (!trimRight(raw).empty()) {
                    line(trimRight(raw));
                }
                continue;
            }
            const std::optional<detail::ElementLine> element = parseElementLine(raw);
            if (!element) {
                line(trimRight(raw));
                continue;
            }
            std::string text(element->name);
            text += ' ';
            text += node(element->nodes[0]);
            text += ' ';
            text += node(element->nodes[1]);
            text += trimRight(element->rest);
            line(text);
        }
    }

    std::string take() { return std::move(text_); }

private:
    std::set<std::string> groundNets_; // lower case
    std::string text_;
};

} // namespace

std::string buildNetlist(const Circuit& circuit)
{
    std::vector<const Component*> components;
    for (const Component& component : circuit.components) {
        components.push_back(&component);
    }
    std::stable_sort(components.begin(), components.end(),
                     [](const Component* a, const Component* b) {
                         return naturalLess(a->designator, b->designator);
                     });
    std::vector<const Fragment*> fragments;
    for (const Fragment& fragment : circuit.fragments) {
        fragments.push_back(&fragment);
    }
    std::stable_sort(fragments.begin(), fragments.end(), [](const Fragment* a, const Fragment* b) {
        return naturalLess(a->name, b->name);
    });

    Writer writer(circuit);
    writer.line(kNetlistTitle);
    for (const Component* component : components) {
        writer.component(*component);
    }
    for (const Fragment* fragment : fragments) {
        writer.fragment(*fragment);
    }
    return writer.take();
}

} // namespace proton::schematic
