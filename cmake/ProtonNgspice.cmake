# SPDX-License-Identifier: AGPL-3.0-only
# ngspice, fetched by tools/fetch-ngspice.ps1 into third_party/ngspice (ADR 0002).
# Proton loads ngspice.dll at runtime, so nothing links against it: builds only need the header,
# and binaries that use ngspice need the DLLs next to them.
#
# Sets PROTON_NGSPICE_FOUND. When found, defines:
#   proton::ngspice_headers            imported target carrying the sharedspice.h include path
#   PROTON_NGSPICE_VERSION             the fetched version, from third_party/ngspice/VERSION
#   proton_copy_ngspice_dlls(<target>) copies the DLLs next to <target> after each build
# Windows only for now; elsewhere ngspice is never found.

option(PROTON_REQUIRE_NGSPICE "Fail configuration if ngspice has not been fetched (on in Windows CI)" OFF)
set(PROTON_NGSPICE_DIR "${PROJECT_SOURCE_DIR}/third_party/ngspice" CACHE PATH
    "Where tools/fetch-ngspice.ps1 put ngspice")

set(PROTON_NGSPICE_FOUND OFF)
if(WIN32)
    find_path(PROTON_NGSPICE_INCLUDE_DIR ngspice/sharedspice.h
        PATHS "${PROTON_NGSPICE_DIR}/include" NO_DEFAULT_PATH)
    find_file(PROTON_NGSPICE_DLL ngspice.dll
        PATHS "${PROTON_NGSPICE_DIR}/bin" NO_DEFAULT_PATH)
    find_file(PROTON_NGSPICE_OMP_DLL libomp140.x86_64.dll
        PATHS "${PROTON_NGSPICE_DIR}/bin" NO_DEFAULT_PATH)
    if(PROTON_NGSPICE_INCLUDE_DIR AND PROTON_NGSPICE_DLL AND PROTON_NGSPICE_OMP_DLL
       AND EXISTS "${PROTON_NGSPICE_DIR}/VERSION")
        set(PROTON_NGSPICE_FOUND ON)
    endif()
endif()

if(PROTON_NGSPICE_FOUND)
    file(STRINGS "${PROTON_NGSPICE_DIR}/VERSION" PROTON_NGSPICE_VERSION LIMIT_COUNT 1)
    message(STATUS "ngspice ${PROTON_NGSPICE_VERSION}: ${PROTON_NGSPICE_DIR}")

    add_library(proton::ngspice_headers INTERFACE IMPORTED)
    target_include_directories(proton::ngspice_headers INTERFACE "${PROTON_NGSPICE_INCLUDE_DIR}")

    function(proton_copy_ngspice_dlls target)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${PROTON_NGSPICE_DLL}" "${PROTON_NGSPICE_OMP_DLL}" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endfunction()
elseif(PROTON_REQUIRE_NGSPICE)
    message(FATAL_ERROR
        "ngspice not found in ${PROTON_NGSPICE_DIR}. Run tools/fetch-ngspice.ps1 (Windows only).")
else()
    message(STATUS "ngspice not found; building without it (run tools/fetch-ngspice.ps1 on Windows)")
endif()
