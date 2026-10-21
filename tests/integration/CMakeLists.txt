# SPDX-License-Identifier: AGPL-3.0-only
add_executable(proton_integration_tests
    test_rc_pipeline.cpp
)
target_link_libraries(proton_integration_tests
    PRIVATE proton::spike_pipeline proton::test_support proton_warnings)
# Golden netlists live in the source tree; a mismatch writes the actual text to the build tree.
target_compile_definitions(proton_integration_tests PRIVATE
    PROTON_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden/integration"
    PROTON_GOLDEN_ACTUAL_DIR="${CMAKE_CURRENT_BINARY_DIR}/golden-actual")

add_test(NAME integration COMMAND proton_integration_tests)
# The demo itself runs to completion: a triggered frame and both readings, exit code 0.
add_test(NAME proton_spike_runs COMMAND proton-spike --duration 0.25)
