# SPDX-License-Identifier: AGPL-3.0-only
add_executable(proton_core_tests
    test_version.cpp
)
target_link_libraries(proton_core_tests PRIVATE proton::core proton_warnings)
target_compile_definitions(proton_core_tests PRIVATE PROTON_EXPECTED_VERSION="${PROJECT_VERSION}")

add_test(NAME core COMMAND proton_core_tests)
