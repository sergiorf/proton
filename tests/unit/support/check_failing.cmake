# SPDX-License-Identifier: AGPL-3.0-only
# Runs the deliberately failing test executable (-DEXE=...) and checks that it exits with 1 and
# reports each failure with file, line and both values. The line numbers below are those of the
# checks in failing_test.cpp; update them together.

execute_process(COMMAND "${EXE}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
message("${output}")

if(NOT result EQUAL 1)
    message(FATAL_ERROR "expected exit code 1, got '${result}'")
endif()

set(expected
    "failing_test\\.cpp:11: error in test 'deliberate_eq_failure': CHECK_EQ\\(answer, 42\\) failed"
    "    value:    41\n    expected: 42\n"
    "failing_test\\.cpp:16: error in test 'deliberate_near_failure': CHECK_NEAR\\(0\\.75, 0\\.7071, 0\\.01\\) failed"
    "    value:      0\\.75\n    expected:   0\\.7071\n    tolerance:  0\\.01\n"
    "failing_test\\.cpp:19: error in test 'deliberate_exception': unexpected exception: boom"
    "\\[ OK \\] passing_neighbour"
    "4 tests, 3 failed"
)
foreach(pattern IN LISTS expected)
    if(NOT output MATCHES "${pattern}")
        message(FATAL_ERROR "output does not match: ${pattern}")
    endif()
endforeach()
