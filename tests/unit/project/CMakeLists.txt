# SPDX-License-Identifier: AGPL-3.0-only
# Project files and the examples in examples/, which the tests read from the source tree. Golden
# netlists live there too; a mismatch writes the actual text to the build tree.
add_executable(proton_project_tests
    test_examples.cpp
    test_project.cpp
)
target_link_libraries(proton_project_tests
    PRIVATE proton::project proton::test_support proton_warnings)
target_compile_definitions(proton_project_tests PRIVATE
    PROTON_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples"
    PROTON_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden/examples"
    PROTON_GOLDEN_ACTUAL_DIR="${CMAKE_CURRENT_BINARY_DIR}/golden-actual")

add_test(NAME project COMMAND proton_project_tests)

# The examples on ngspice, in their own process: ngspice keeps process-wide state.
if(PROTON_NGSPICE_FOUND)
    add_executable(proton_project_ngspice_tests test_examples_ngspice.cpp)
    target_link_libraries(proton_project_ngspice_tests
        PRIVATE proton::project proton::test_support proton_warnings)
    target_compile_definitions(proton_project_ngspice_tests PRIVATE
        PROTON_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
    proton_copy_ngspice_dlls(proton_project_ngspice_tests)
    add_test(NAME examples_ngspice COMMAND proton_project_ngspice_tests)
endif()
