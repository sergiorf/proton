// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace proton::schematic {

// Parses a SPICE number the way ngspice reads element values: an optional sign, a decimal
// mantissa, an optional exponent (`e3`, `E-6`), an optional scale factor, then any letters, which
// are ignored as units. Scale factors are case-insensitive:
//
//     t 1e12   g 1e9   meg 1e6   k 1e3   mil 25.4e-6   m 1e-3   u 1e-6   n 1e-9   p 1e-12   f 1e-15
//
// The SPICE traps follow from this: `m` and `M` are milli (`1MHz` is 1 mHz, mega is `meg`), and a
// unit that starts with a scale letter scales the value (`10F` is 10 femto, `10uF` is 10 micro).
//
//     parseSpiceNumber("4.7k")   -> 4700
//     parseSpiceNumber("10uF")   -> 10e-6
//     parseSpiceNumber("1MEG")   -> 1e6
//     parseSpiceNumber("2.5e3k") -> 2.5e6
//
// Returns nullopt for anything else: empty text, no mantissa digits, surrounding whitespace,
// non-letters after the number (`1k2`, `1,5`, `1µF`), or a result that overflows or underflows a
// double. Except for `mil`, the result is the double nearest to the written decimal value. Does
// not depend on the C locale.
std::optional<double> parseSpiceNumber(std::string_view text);

// Writes `value` as a SPICE number with the shortest mantissa that parses back to exactly the
// same double, using a lowercase scale factor so the mantissa is in [1, 1000):
//
//     formatSpiceNumber(4700)    -> "4.7k"
//     formatSpiceNumber(1e6)     -> "1meg"
//     formatSpiceNumber(2.2e-6)  -> "2.2u"
//     formatSpiceNumber(0.5)     -> "500m"
//
// `mil` is never written. Magnitudes outside the scale factors' range use an exponent ("1e15",
// "2.5e-18"). Zero is "0". NaN and infinities have no SPICE form and give an empty string, which
// parseSpiceNumber rejects. Does not depend on the C locale.
std::string formatSpiceNumber(double value);

} // namespace proton::schematic
