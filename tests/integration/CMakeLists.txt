# SPDX-License-Identifier: AGPL-3.0-only
# The pipeline test, built once per engine: test_rc_pipeline.cpp runs on the mock, or on ngspice
# when PROTON_TEST_NGSPICE is defined.
function(proton_add_pipeline_test target)
    add_executable(${target} test_rc_pipeline.cpp)
    target_link_libraries(${target}
        PRIVATE proton::spike_pipeline proton::test_support proton_warnings)
    # Golden netlists live in the source tree; a mismatch writes the actual text to the build tree.
    target_compile_definitions(${target} PRIVATE
        PROTON_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden/integration"
        PROTON_GOLDEN_ACTUAL_DIR="${CMAKE_CURRENT_BINARY_DIR}/golden-actual")
endfunction()

proton_add_pipeline_test(proton_integration_tests)
add_test(NAME integration COMMAND proton_integration_tests)
# The demo itself runs to completion: a triggered frame and both readings, exit code 0.
add_test(NAME proton_spike_runs COMMAND proton-spike --duration 0.25)

# ngspice (M1.3), each executable in its own process: ngspice keeps process-wide state.
if(PROTON_NGSPICE_FOUND)
    proton_add_pipeline_test(proton_integration_ngspice_tests)
    target_compile_definitions(proton_integration_ngspice_tests PRIVATE PROTON_TEST_NGSPICE)
    proton_copy_ngspice_dlls(proton_integration_ngspice_tests)
    add_test(NAME integration_ngspice COMMAND proton_integration_ngspice_tests)

    add_executable(proton_ngspice_validation_tests test_ngspice_validation.cpp)
    target_link_libraries(proton_ngspice_validation_tests PRIVATE
        proton::sim proton::schematic proton::instruments proton::test_support proton_warnings)
    proton_copy_ngspice_dlls(proton_ngspice_validation_tests)
    add_test(NAME ngspice_validation COMMAND proton_ngspice_validation_tests)
endif()
