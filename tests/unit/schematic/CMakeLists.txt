# SPDX-License-Identifier: AGPL-3.0-only
add_executable(proton_schematic_tests
    test_circuit.cpp
    test_netlist.cpp
    test_units.cpp
    test_validate.cpp
)
target_link_libraries(proton_schematic_tests
    PRIVATE proton::schematic proton::test_support proton_warnings)
# Golden netlists live in the source tree; a mismatch writes the actual text to the build tree.
target_compile_definitions(proton_schematic_tests PRIVATE
    PROTON_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden/netlist"
    PROTON_GOLDEN_ACTUAL_DIR="${CMAKE_CURRENT_BINARY_DIR}/golden-actual")

add_test(NAME schematic COMMAND proton_schematic_tests)
