// SPDX-License-Identifier: AGPL-3.0-only
// Placeholder test until the harness in tests/support exists (M0.2).
#include "proton/core/version.h"

#include <cstdio>

int main()
{
    if (proton::core::versionString() != PROTON_EXPECTED_VERSION) {
        std::fprintf(stderr, "versionString() returned '%.*s', expected '%s'\n",
                     static_cast<int>(proton::core::versionString().size()),
                     proton::core::versionString().data(), PROTON_EXPECTED_VERSION);
        return 1;
    }
    return 0;
}
