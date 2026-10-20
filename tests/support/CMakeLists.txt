# SPDX-License-Identifier: AGPL-3.0-only
# proton::test_support: the test harness, including main(). Link it into every test executable.
add_library(proton_test_support STATIC
    src/test.cpp
    src/main.cpp
)
add_library(proton::test_support ALIAS proton_test_support)

target_include_directories(proton_test_support PUBLIC include)
target_link_libraries(proton_test_support PRIVATE proton_warnings)
