# SPDX-License-Identifier: AGPL-3.0-only
# Applies PROTON_SANITIZE to everything in the build, tests included, so that all code
# that runs under a sanitizer is instrumented.

if(NOT PROTON_SANITIZE)
    return()
endif()

if("thread" IN_LIST PROTON_SANITIZE AND "address" IN_LIST PROTON_SANITIZE)
    message(FATAL_ERROR "PROTON_SANITIZE: thread cannot be combined with address")
endif()

if(MSVC)
    if(NOT PROTON_SANITIZE STREQUAL "address")
        message(FATAL_ERROR "PROTON_SANITIZE: MSVC supports only 'address', got '${PROTON_SANITIZE}'")
    endif()
    add_compile_options(/fsanitize=address)
else()
    list(JOIN PROTON_SANITIZE "," _proton_sanitizers)
    add_compile_options(-fsanitize=${_proton_sanitizers} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=${_proton_sanitizers})
endif()

message(STATUS "Proton: sanitizers enabled: ${PROTON_SANITIZE}")
