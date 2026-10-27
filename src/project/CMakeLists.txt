# SPDX-License-Identifier: AGPL-3.0-only
# Project files (PLAN.md M4.11): a drawing, its instruments and their settings as versioned JSON,
# and the examples folder. Qt-free, so the app and the tests share one format.
add_library(proton_project
    src/project.cpp
)
add_library(proton::project ALIAS proton_project)

target_include_directories(proton_project PUBLIC include)
target_link_libraries(proton_project
    PUBLIC proton::bench proton::schematic
    PRIVATE nlohmann_json::nlohmann_json proton_warnings)
