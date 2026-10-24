# SPDX-License-Identifier: AGPL-3.0-only
add_executable(proton_sim_tests
    test_mock_engine.cpp
    test_real_time_pacer.cpp
    test_sim_chunk.cpp
    test_sim_worker.cpp
)
target_link_libraries(proton_sim_tests PRIVATE proton::sim proton::test_support proton_warnings)

add_test(NAME sim COMMAND proton_sim_tests)

# The fetched ngspice library (M1.1, ADR 0008 on Linux) and NgspiceEngine (M1.2). Each is its own executable: ngspice
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

    # Fatal-error reloads (M1.4). Each reload leaks ngspice's memory from a library that is
    # unloaded before LeakSanitizer can name it for a suppression, so leak detection is off for
    # this executable only; ASan's other checks, UBSan and TSan still run (ADR 0008).
    add_executable(proton_ngspice_reload_tests test_ngspice_reload.cpp)
    target_link_libraries(proton_ngspice_reload_tests
        PRIVATE proton::sim proton::test_support proton_warnings)
    proton_copy_ngspice_dlls(proton_ngspice_reload_tests)
    add_test(NAME ngspice_reload COMMAND proton_ngspice_reload_tests)
    if(NOT WIN32)
        set_tests_properties(ngspice_reload PROPERTIES
            ENVIRONMENT_MODIFICATION "ASAN_OPTIONS=string_append::detect_leaks=0")
        # Again on one core: there ngspice's ended thread only finishes once the caller blocks,
        # which made freeing the library under it crash on every run, not once in a while.
        find_program(PROTON_TASKSET taskset)
        if(PROTON_TASKSET)
            add_test(NAME ngspice_reload_one_core
                COMMAND "${PROTON_TASKSET}" -c 0 $<TARGET_FILE:proton_ngspice_reload_tests>)
            set_tests_properties(ngspice_reload_one_core PROPERTIES
                ENVIRONMENT_MODIFICATION "ASAN_OPTIONS=string_append::detect_leaks=0")
        endif()
    endif()

    # Memory over many runs and fatal-error reloads (M1.4). Windows only: it reads the process's
    # committed memory, which under the Linux sanitizers would measure their allocators instead.
    if(WIN32)
        add_executable(proton_ngspice_soak_tests test_ngspice_soak.cpp)
        target_link_libraries(proton_ngspice_soak_tests
            PRIVATE proton::sim proton::test_support proton_warnings)
        proton_copy_ngspice_dlls(proton_ngspice_soak_tests)
        add_test(NAME ngspice_soak COMMAND proton_ngspice_soak_tests)
    endif()
endif()
