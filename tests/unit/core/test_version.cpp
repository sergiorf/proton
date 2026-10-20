// SPDX-License-Identifier: AGPL-3.0-only
#include "proton/core/version.h"

#include "proton/test/test.h"

#include <string_view>

TEST(version_string_matches_project_version)
{
    CHECK_EQ(proton::core::versionString(), std::string_view{PROTON_EXPECTED_VERSION});
}
