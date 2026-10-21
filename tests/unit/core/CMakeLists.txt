# SPDX-License-Identifier: AGPL-3.0-only
find_package(Threads REQUIRED)

add_executable(proton_core_tests
    test_format.cpp
    test_spice_number.cpp
    test_spsc_ring.cpp
    test_version.cpp
)
target_link_libraries(proton_core_tests
    PRIVATE proton::core proton::test_support proton_warnings Threads::Threads)
target_compile_definitions(proton_core_tests PRIVATE PROTON_EXPECTED_VERSION="${PROJECT_VERSION}")

add_test(NAME core COMMAND proton_core_tests)
