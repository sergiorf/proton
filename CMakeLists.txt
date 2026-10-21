# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.25)

project(Proton VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(PROTON_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (on in CI)" OFF)
set(PROTON_SANITIZE "" CACHE STRING "Sanitizers to enable: address, address;undefined, or thread")

include(ProtonWarnings)
include(ProtonSanitizers)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/schematic)
add_subdirectory(src/instruments)
add_subdirectory(tests)
