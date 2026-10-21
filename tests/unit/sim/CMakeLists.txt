# SPDX-License-Identifier: AGPL-3.0-only
add_executable(proton_sim_tests
    test_mock_engine.cpp
    test_sim_chunk.cpp
)
target_link_libraries(proton_sim_tests PRIVATE proton::sim proton::test_support proton_warnings)

add_test(NAME sim COMMAND proton_sim_tests)
