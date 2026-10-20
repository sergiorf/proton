// SPDX-License-Identifier: AGPL-3.0-only
#pragma once

#include <string_view>

namespace proton::core {

// Proton's version as "major.minor.patch", taken from the CMake project version.
std::string_view versionString() noexcept;

} // namespace proton::core
