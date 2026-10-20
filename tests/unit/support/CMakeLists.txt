# SPDX-License-Identifier: AGPL-3.0-only
add_executable(proton_support_tests
    test_harness.cpp
)
target_link_libraries(proton_support_tests PRIVATE proton::test_support proton_warnings)
add_test(NAME support COMMAND proton_support_tests)

# A deliberately failing executable: the test passes when it fails the expected way.
add_executable(proton_support_failing_tests
    failing_test.cpp
)
target_link_libraries(proton_support_failing_tests PRIVATE proton::test_support proton_warnings)
add_test(NAME support_failure_report
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:proton_support_failing_tests>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_failing.cmake
)
