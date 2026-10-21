# SPDX-License-Identifier: AGPL-3.0-only
add_library(proton_instruments
    src/function_generator.cpp
)
add_library(proton::instruments ALIAS proton_instruments)

target_include_directories(proton_instruments PUBLIC include)
target_link_libraries(proton_instruments PRIVATE proton::core proton_warnings)
