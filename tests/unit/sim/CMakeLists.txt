# SPDX-License-Identifier: AGPL-3.0-only
add_executable(proton_sim_tests
    test_mock_engine.cpp
    test_sim_chunk.cpp
    test_sim_worker.cpp
)
target_link_libraries(proton_sim_tests PRIVATE proton::sim proton::test_support proton_warnings)

add_test(NAME sim COMMAND proton_sim_tests)

# The fetched ngspice DLL (M1.1) and NgspiceEngine (M1.2). Each is its own executable: ngspice
# keeps process-wide state.
if(PROTON_NGSPICE_FOUND)
    add_executable(proton_ngspice_dll_tests test_ngspice_dll.cpp)
    target_link_libraries(proton_ngspice_dll_tests
        PRIVATE proton::ngspice_headers proton::test_support proton_warnings)
    target_compile_definitions(proton_ngspice_dll_tests
        PRIVATE PROTON_NGSPICE_VERSION="${PROTON_NGSPICE_VERSION}")
    proton_copy_ngspice_dlls(proton_ngspice_dll_tests)
    add_test(NAME ngspice_dll COMMAND proton_ngspice_dll_tests)

    add_executable(proton_ngspice_engine_tests test_ngspice_engine.cpp)
    target_link_libraries(proton_ngspice_engine_tests
        PRIVATE proton::sim proton::test_support proton_warnings)
    proton_copy_ngspice_dlls(proton_ngspice_engine_tests)
    add_test(NAME ngspice_engine COMMAND proton_ngspice_engine_tests)
endif()
