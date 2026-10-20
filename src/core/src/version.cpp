// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/core/version.h"

namespace proton::core {

std::string_view versionString() noexcept
{
    return PROTON_VERSION_STRING;
}

} // namespace proton::core
