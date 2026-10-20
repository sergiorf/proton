# SPDX-License-Identifier: AGPL-3.0-only
add_executable(proton_schematic_tests
    test_circuit.cpp
    test_units.cpp
)
target_link_libraries(proton_schematic_tests
    PRIVATE proton::schematic proton::test_support proton_warnings)

add_test(NAME schematic COMMAND proton_schematic_tests)
