// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

// Private helpers shared by validate() and buildNetlist(), so both read names and fragment lines
// the same way.

#include "proton/schematic/circuit.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace proton::schematic::detail {

inline char toLower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline char toUpper(char c)
{
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

inline bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

inline std::string lower(std::string_view text)
{
    std::string result(text);
    for (char& c : result) {
        c = toLower(c);
    }
    return result;
}

// Characters allowed in designators and net names: what SPICE reads as part of one token.
inline bool isNameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '+' || c == '.';
}

inline bool isValidName(std::string_view name)
{
    if (name.empty()) {
        return false;
    }
    for (const char c : name) {
        if (!isNameChar(c)) {
            return false;
        }
    }
    return true;
}

// The SPICE element letter of a kind, upper case; 0 for Ground, which is not an element.
inline char spiceLetter(ComponentKind kind)
{
    switch (kind) {
    case ComponentKind::Resistor:
        return 'R';
    case ComponentKind::Capacitor:
        return 'C';
    case ComponentKind::Inductor:
        return 'L';
    case ComponentKind::VoltageSource:
        return 'V';
    case ComponentKind::CurrentSource:
        return 'I';
    case ComponentKind::Ground:
        return 0;
    }
    return 0;
}

// Blank lines and `*` comments in a fragment carry no element.
inline bool isBlankOrComment(std::string_view line)
{
    for (const char c : line) {
        if (!isSpace(c)) {
            return c == '*';
        }
    }
    return true;
}

// A fragment line read as `<name> <node> <node> <rest>`. The rest starts right after the second
// node, with its leading whitespace.
struct ElementLine
{
    std::string_view name;
    char letter = 0; // upper case
    std::string_view nodes[2];
    std::string_view rest;
};

// Reads an R, C, L, V or I element line; nullopt for any other line.
inline std::optional<ElementLine> parseElementLine(std::string_view line)
{
    std::string_view tokens[3];
    std::size_t pos = 0;
    for (std::string_view& token : tokens) {
        while (pos < line.size() && isSpace(line[pos])) {
            ++pos;
        }
        const std::size_t start = pos;
        while (pos < line.size() && !isSpace(line[pos])) {
            ++pos;
        }
        if (pos == start) {
            return std::nullopt;
        }
        token = line.substr(start, pos - start);
    }
    const char letter = toUpper(tokens[0].front());
    if (letter != 'R' && letter != 'C' && letter != 'L' && letter != 'V' && letter != 'I') {
        return std::nullopt;
    }
    return ElementLine{tokens[0], letter, {tokens[1], tokens[2]}, line.substr(pos)};
}

} // namespace proton::schematic::detail
