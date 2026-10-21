// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/schematic/validate.h"

#include "proton/core/spice_number.h"
#include "spice_line.h"

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace proton::schematic {

namespace {

// A SPICE element as the checks see it, from a component or a fragment line.
struct Element
{
    std::string name;
    std::string fragment; // name of the fragment it comes from; empty for a component
    char letter = 0;      // SPICE element letter, upper case
    std::vector<std::string> nets;
    bool netsValid = false;
};

using core::parseSpiceNumber;
using detail::isBlankOrComment;
using detail::isValidName;
using detail::lower;
using detail::parseElementLine;
using detail::spiceLetter;
using detail::toUpper;

std::string_view kindName(ComponentKind kind)
{
    switch (kind) {
    case ComponentKind::Resistor:
        return "resistor";
    case ComponentKind::Capacitor:
        return "capacitor";
    case ComponentKind::Inductor:
        return "inductor";
    case ComponentKind::VoltageSource:
        return "voltage source";
    case ComponentKind::CurrentSource:
        return "current source";
    case ComponentKind::Ground:
        return "ground";
    }
    return "component";
}

// Capacitors are open at DC and current sources are ideal, so neither gives a DC path.
bool conductsDc(char letter)
{
    return letter == 'R' || letter == 'L' || letter == 'V';
}

bool hasValidNets(const std::vector<std::string>& nets, std::size_t count)
{
    if (nets.size() != count) {
        return false;
    }
    for (const std::string& net : nets) {
        if (net.empty()) {
            return false;
        }
    }
    return true;
}

constexpr const char* kNameCharacters = "letters, digits and _ - + .";

std::string describe(const Element& element)
{
    std::string text = element.name;
    if (!element.fragment.empty()) {
        text += " (from ";
        text += element.fragment;
        text += ')';
    }
    return text;
}

// Nets keyed case-insensitively, in the order they first appear, with union-find for DC paths.
class Nets
{
public:
    std::size_t add(const std::string& net)
    {
        const auto [it, inserted] = index_.try_emplace(lower(net), spelling_.size());
        if (inserted) {
            spelling_.push_back(net);
            parent_.push_back(spelling_.size() - 1);
            connections_.push_back(0);
        }
        return it->second;
    }

    std::optional<std::size_t> find(std::string_view net) const
    {
        const auto it = index_.find(lower(net));
        if (it == index_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void connect(std::size_t a, std::size_t b) { parent_[root(a)] = root(b); }
    bool joined(std::size_t a, std::size_t b) { return root(a) == root(b); }

    std::size_t size() const { return spelling_.size(); }
    const std::string& spelling(std::size_t net) const { return spelling_[net]; }
    int& connections(std::size_t net) { return connections_[net]; }

private:
    std::size_t root(std::size_t net)
    {
        while (parent_[net] != net) {
            parent_[net] = parent_[parent_[net]];
            net = parent_[net];
        }
        return net;
    }

    std::map<std::string, std::size_t> index_;
    std::vector<std::string> spelling_;
    std::vector<std::size_t> parent_;
    std::vector<int> connections_;
};

class Validator
{
public:
    explicit Validator(const Circuit& circuit) : circuit_(circuit) {}

    std::vector<Diagnostic> run()
    {
        collectElements();
        checkGround();
        checkDesignators();
        checkDuplicates();
        checkTerminals();
        checkNetNames();
        checkValues();
        checkFragmentLines();
        checkConnectivity();
        return std::move(diagnostics_);
    }

private:
    void report(Severity severity, DiagnosticCode code, std::string element, std::string net,
                std::string message)
    {
        diagnostics_.push_back(
            {severity, code, std::move(element), std::move(net), std::move(message)});
    }

    void collectElements()
    {
        for (const Component& component : circuit_.components) {
            const bool netsValid = hasValidNets(component.nets, terminalCountOf(component));
            if (component.kind == ComponentKind::Ground) {
                if (netsValid) {
                    groundNets_.push_back(component.nets.front());
                }
                continue;
            }
            elements_.push_back(
                {component.designator, {}, spiceLetter(component.kind), component.nets, netsValid});
        }
        for (const Fragment& fragment : circuit_.fragments) {
            for (const std::string& line : fragment.lines) {
                if (isBlankOrComment(line)) {
                    continue;
                }
                const std::optional<detail::ElementLine> parsed = parseElementLine(line);
                if (!parsed) {
                    badFragmentLines_.emplace_back(&fragment, &line);
                    continue;
                }
                elements_.push_back({std::string(parsed->name),
                                     fragment.name,
                                     parsed->letter,
                                     {std::string(parsed->nodes[0]), std::string(parsed->nodes[1])},
                                     true});
            }
        }
    }

    static std::size_t terminalCountOf(const Component& component)
    {
        return static_cast<std::size_t>(terminalCount(component.kind));
    }

    void checkGround()
    {
        bool onNodeZero = false;
        for (const Element& element : elements_) {
            for (const std::string& net : element.nets) {
                onNodeZero = onNodeZero || (element.netsValid && net == kGroundNet);
            }
        }
        if (onNodeZero) {
            groundNets_.emplace_back(kGroundNet);
        }
        if (groundNets_.empty()) {
            report(Severity::Error, DiagnosticCode::NoGround, {}, {},
                   "The circuit has no ground. Add a ground symbol: every voltage is measured "
                   "against it.");
        }
    }

    void checkDesignators()
    {
        for (const Component& component : circuit_.components) {
            if (component.kind == ComponentKind::Ground) {
                continue;
            }
            const std::string& name = component.designator;
            const char letter = spiceLetter(component.kind);
            if (name.empty()) {
                report(Severity::Error, DiagnosticCode::InvalidDesignator, name, {},
                       "A " + std::string(kindName(component.kind)) + " has no designator.");
            } else if (!isValidName(name)) {
                report(Severity::Error, DiagnosticCode::InvalidDesignator, name, {},
                       "Designator '" + name + "' may only contain " + kNameCharacters + ".");
            } else if (toUpper(name.front()) != letter) {
                report(Severity::Error, DiagnosticCode::InvalidDesignator, name, {},
                       "Designator '" + name + "' of a " + std::string(kindName(component.kind)) +
                           " must start with " + std::string(1, letter) + ".");
            }
        }
    }

    void checkDuplicates()
    {
        std::map<std::string, const Element*> seen;
        for (const Element& element : elements_) {
            if (element.name.empty()) {
                continue;
            }
            const auto [it, inserted] = seen.try_emplace(lower(element.name), &element);
            if (!inserted) {
                report(Severity::Error, DiagnosticCode::DuplicateDesignator, element.name, {},
                       "Designator " + describe(element) + " is already used by " +
                           describe(*it->second) + "; names ignore case.");
            }
        }
    }

    void checkTerminals()
    {
        for (const Component& component : circuit_.components) {
            if (hasValidNets(component.nets, terminalCountOf(component))) {
                continue;
            }
            std::string who = component.designator;
            if (component.kind == ComponentKind::Ground || who.empty()) {
                who = "A " + std::string(kindName(component.kind));
            }
            report(Severity::Error, DiagnosticCode::InvalidTerminals, component.designator, {},
                   who + " has an unconnected terminal.");
        }
    }

    // Empty net names are already InvalidTerminals; each bad name is reported once.
    void checkNetNames()
    {
        std::set<std::string> reported;
        const auto check = [&](const std::string& element, const std::string& net) {
            if (net.empty() || isValidName(net) || !reported.insert(lower(net)).second) {
                return;
            }
            report(Severity::Error, DiagnosticCode::InvalidNetName, element, net,
                   "Net name '" + net + "' may only contain " + kNameCharacters + ".");
        };
        for (const Component& component : circuit_.components) {
            for (const std::string& net : component.nets) {
                check(component.designator, net);
            }
        }
        for (const Element& element : elements_) {
            if (!element.fragment.empty()) {
                check(element.name, element.nets[0]);
                check(element.name, element.nets[1]);
            }
        }
    }

    void checkValues()
    {
        for (const Component& component : circuit_.components) {
            if (component.kind == ComponentKind::Ground) {
                continue;
            }
            const std::optional<double> value = parseSpiceNumber(component.value);
            if (!value) {
                report(Severity::Error, DiagnosticCode::InvalidValue, component.designator, {},
                       component.value.empty()
                           ? component.designator + " has no value."
                           : component.designator + " has value '" + component.value +
                                 "', which is not a number such as 4.7k or 100n.");
                continue;
            }
            const bool passive = component.kind == ComponentKind::Resistor ||
                                 component.kind == ComponentKind::Capacitor ||
                                 component.kind == ComponentKind::Inductor;
            if (passive && *value == 0.0) {
                report(Severity::Error, DiagnosticCode::ZeroValue, component.designator, {},
                       component.designator + " has a value of zero.");
            }
        }
    }

    void checkFragmentLines()
    {
        for (const auto& [fragment, line] : badFragmentLines_) {
            report(Severity::Error, DiagnosticCode::InvalidFragmentLine, {}, {},
                   "Instrument " + fragment->name + " produced a line that cannot be checked: '" +
                       *line + "'.");
        }
    }

    void checkConnectivity()
    {
        Nets nets;
        for (const Element& element : elements_) {
            if (!element.netsValid) {
                continue;
            }
            const std::size_t a = nets.add(element.nets[0]);
            const std::size_t b = nets.add(element.nets[1]);
            ++nets.connections(a);
            ++nets.connections(b);
            if (conductsDc(element.letter)) {
                nets.connect(a, b);
            }
        }
        std::vector<bool> isGround(nets.size(), false);
        std::optional<std::size_t> ground;
        for (const std::string& net : groundNets_) {
            const std::optional<std::size_t> index = nets.find(net);
            if (!index) {
                continue; // a ground symbol on a net nothing else uses
            }
            isGround[*index] = true;
            if (ground) {
                nets.connect(*index, *ground);
            }
            ground = index;
        }

        for (std::size_t net = 0; net < nets.size(); ++net) {
            if (!isGround[net] && nets.connections(net) == 1) {
                report(Severity::Warning, DiagnosticCode::SingleConnectionNet, {},
                       nets.spelling(net),
                       "Net " + nets.spelling(net) + " has only one connection.");
            }
        }
        if (groundNets_.empty()) {
            return; // NoGround already says it; every net would be reported
        }
        for (std::size_t net = 0; net < nets.size(); ++net) {
            if (!ground || !nets.joined(net, *ground)) {
                report(Severity::Error, DiagnosticCode::NoDcPathToGround, {}, nets.spelling(net),
                       "Net " + nets.spelling(net) +
                           " has no DC path to ground (capacitors block DC); connect it through "
                           "a resistor, for example.");
            }
        }
    }

    const Circuit& circuit_;
    std::vector<Element> elements_;
    std::vector<std::string> groundNets_;
    std::vector<std::pair<const Fragment*, const std::string*>> badFragmentLines_;
    std::vector<Diagnostic> diagnostics_;
};

} // namespace

std::string_view codeName(DiagnosticCode code)
{
    switch (code) {
    case DiagnosticCode::NoGround:
        return "NoGround";
    case DiagnosticCode::InvalidDesignator:
        return "InvalidDesignator";
    case DiagnosticCode::DuplicateDesignator:
        return "DuplicateDesignator";
    case DiagnosticCode::InvalidTerminals:
        return "InvalidTerminals";
    case DiagnosticCode::InvalidNetName:
        return "InvalidNetName";
    case DiagnosticCode::InvalidValue:
        return "InvalidValue";
    case DiagnosticCode::ZeroValue:
        return "ZeroValue";
    case DiagnosticCode::InvalidFragmentLine:
        return "InvalidFragmentLine";
    case DiagnosticCode::SingleConnectionNet:
        return "SingleConnectionNet";
    case DiagnosticCode::NoDcPathToGround:
        return "NoDcPathToGround";
    }
    return "Unknown";
}

std::vector<Diagnostic> validate(const Circuit& circuit)
{
    return Validator(circuit).run();
}

bool hasErrors(const std::vector<Diagnostic>& diagnostics)
{
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == Severity::Error) {
            return true;
        }
    }
    return false;
}

} // namespace proton::schematic
