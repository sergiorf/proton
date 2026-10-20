# SPDX-License-Identifier: AGPL-3.0-only
add_library(proton_core
    src/version.cpp
)
add_library(proton::core ALIAS proton_core)

target_include_directories(proton_core PUBLIC include)
target_compile_definitions(proton_core PRIVATE PROTON_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(proton_core PRIVATE proton_warnings)
