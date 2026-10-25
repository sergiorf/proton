# SPDX-License-Identifier: AGPL-3.0-only
add_executable(proton_bench_tests
    test_bench.cpp
    test_examples.cpp
    test_run_control.cpp
)
target_link_libraries(proton_bench_tests PRIVATE proton::bench proton::test_support proton_warnings)
# Golden netlists live in the source tree; a mismatch writes the actual text to the build tree.
target_compile_definitions(proton_bench_tests PRIVATE
    PROTON_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden/bench"
    PROTON_GOLDEN_ACTUAL_DIR="${CMAKE_CURRENT_BINARY_DIR}/golden-actual")

add_test(NAME bench COMMAND proton_bench_tests)

# The examples on ngspice, in their own process: ngspice keeps process-wide state.
if(PROTON_NGSPICE_FOUND)
    add_executable(proton_bench_ngspice_tests test_examples_ngspice.cpp)
    target_link_libraries(proton_bench_ngspice_tests
        PRIVATE proton::bench proton::test_support proton_warnings)
    proton_copy_ngspice_dlls(proton_bench_ngspice_tests)
    add_test(NAME bench_ngspice COMMAND proton_bench_ngspice_tests)
endif()
