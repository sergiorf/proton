# SPDX-License-Identifier: AGPL-3.0-only
# Repository checks: rules about the source tree rather than about a module.

set(check_spdx ${PROJECT_SOURCE_DIR}/tools/check-spdx.cmake)

add_test(NAME spdx_headers
    COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${check_spdx}
)

# The check itself fails on a file without the SPDX line.
add_test(NAME spdx_headers_detects_missing
    COMMAND ${CMAKE_COMMAND} -DCHECK=${check_spdx} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/spdx_fixture
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_spdx_detects_missing.cmake
)
