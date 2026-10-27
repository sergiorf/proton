# SPDX-License-Identifier: AGPL-3.0-only
add_executable(proton_bench_tests
    test_bench.cpp
    test_run_control.cpp
    test_sheet_bench.cpp
)
target_link_libraries(proton_bench_tests PRIVATE proton::bench proton::test_support proton_warnings)

add_test(NAME bench COMMAND proton_bench_tests)
