# SPDX-License-Identifier: AGPL-3.0-only
# ngspice in third_party/ngspice: on Windows the official DLL, fetched by tools/fetch-ngspice.ps1
# (ADR 0002); on Linux libngspice.so, built from the same release by tools/fetch-ngspice.sh for
# the sanitizer CI jobs (ADR 0008). Proton loads it at runtime, so nothing links against it:
# builds only need the header, and binaries that use ngspice need the library next to them.
#
# Sets PROTON_NGSPICE_FOUND. When found, defines:
#   proton::ngspice_headers            imported target carrying the sharedspice.h include path
#   PROTON_NGSPICE_VERSION             the fetched version, from third_party/ngspice/VERSION
#   proton_copy_ngspice_dlls(<target>) copies the library files next to <target> after each build

option(PROTON_REQUIRE_NGSPICE "Fail configuration if ngspice has not been fetched (on in Windows CI)" OFF)
set(PROTON_NGSPICE_DIR "${PROJECT_SOURCE_DIR}/third_party/ngspice" CACHE PATH
    "Where tools/fetch-ngspice.ps1 or tools/fetch-ngspice.sh put ngspice")

set(PROTON_NGSPICE_FOUND OFF)
find_path(PROTON_NGSPICE_INCLUDE_DIR ngspice/sharedspice.h
    PATHS "${PROTON_NGSPICE_DIR}/include" NO_DEFAULT_PATH)
if(WIN32)
    find_file(PROTON_NGSPICE_DLL ngspice.dll
        PATHS "${PROTON_NGSPICE_DIR}/bin" NO_DEFAULT_PATH)
    find_file(PROTON_NGSPICE_OMP_DLL libomp140.x86_64.dll
        PATHS "${PROTON_NGSPICE_DIR}/bin" NO_DEFAULT_PATH)
    set(PROTON_NGSPICE_FILES "${PROTON_NGSPICE_DLL}" "${PROTON_NGSPICE_OMP_DLL}")
    set(PROTON_NGSPICE_FETCH tools/fetch-ngspice.ps1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_file(PROTON_NGSPICE_SO libngspice.so
        PATHS "${PROTON_NGSPICE_DIR}/lib" NO_DEFAULT_PATH)
    set(PROTON_NGSPICE_FILES "${PROTON_NGSPICE_SO}")
    set(PROTON_NGSPICE_FETCH tools/fetch-ngspice.sh)
endif()
if(PROTON_NGSPICE_FILES AND NOT "${PROTON_NGSPICE_FILES}" MATCHES "NOTFOUND"
   AND PROTON_NGSPICE_INCLUDE_DIR AND EXISTS "${PROTON_NGSPICE_DIR}/VERSION")
    set(PROTON_NGSPICE_FOUND ON)
endif()

if(PROTON_NGSPICE_FOUND)
    file(STRINGS "${PROTON_NGSPICE_DIR}/VERSION" PROTON_NGSPICE_VERSION LIMIT_COUNT 1)
    message(STATUS "ngspice ${PROTON_NGSPICE_VERSION}: ${PROTON_NGSPICE_DIR}")

    add_library(proton::ngspice_headers INTERFACE IMPORTED)
    target_include_directories(proton::ngspice_headers INTERFACE "${PROTON_NGSPICE_INCLUDE_DIR}")

    function(proton_copy_ngspice_dlls target)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${PROTON_NGSPICE_FILES} "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endfunction()
elseif(PROTON_REQUIRE_NGSPICE)
    message(FATAL_ERROR
        "ngspice not found in ${PROTON_NGSPICE_DIR}. Run ${PROTON_NGSPICE_FETCH}.")
else()
    message(STATUS "ngspice not found; building without it (run ${PROTON_NGSPICE_FETCH})")
endif()
