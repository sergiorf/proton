# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.25)

project(Proton VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(PROTON_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (on in CI)" OFF)
set(PROTON_SANITIZE "" CACHE STRING "Sanitizers to enable: address, address;undefined, or thread")
# The Qt app (src/app), the only part of Proton that needs Qt. Off by default off Windows, so the
# Linux CI jobs, which have no Qt, enforce "no Qt below src/app" (ADR 0001, ADR 0003).
if(WIN32)
    set(PROTON_BUILD_APP_DEFAULT ON)
else()
    set(PROTON_BUILD_APP_DEFAULT OFF)
endif()
option(PROTON_BUILD_APP "Build the Qt app in src/app (needs Qt 6, found through QT_ROOT_DIR)"
    ${PROTON_BUILD_APP_DEFAULT})

include(ProtonWarnings)
include(ProtonSanitizers)
include(ProtonNgspice)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/schematic)
add_subdirectory(src/instruments)
add_subdirectory(src/sim)
add_subdirectory(src/bench)
add_subdirectory(tools/proton-spike)
if(PROTON_BUILD_APP)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets)
    add_subdirectory(src/app)
endif()
add_subdirectory(tests)
