# SPDX-License-Identifier: AGPL-3.0-only
add_executable(proton_instruments_tests
    test_function_generator.cpp
    test_dc_power_supply.cpp
    test_digit_entry.cpp
    test_knob.cpp
    test_knob_steps.cpp
    test_multimeter.cpp
    test_scope.cpp
    test_scope_display.cpp
    test_scope_front_end.cpp
    test_selector.cpp
    test_seven_segment.cpp
)
# schematic only to check that fragments pass validate(); the library itself does not use it.
target_link_libraries(proton_instruments_tests
    PRIVATE proton::instruments proton::schematic proton::test_support proton_warnings)
# Golden fragments live in the source tree; a mismatch writes the actual text to the build tree.
target_compile_definitions(proton_instruments_tests PRIVATE
    PROTON_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden/instruments"
    PROTON_GOLDEN_ACTUAL_DIR="${CMAKE_CURRENT_BINARY_DIR}/golden-actual")

add_test(NAME instruments COMMAND proton_instruments_tests)
