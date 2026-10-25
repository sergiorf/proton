# SPDX-License-Identifier: AGPL-3.0-only
add_library(proton_instruments
    src/function_generator.cpp
    src/knob_steps.cpp
    src/multimeter.cpp
    src/scope.cpp
    src/scope_display.cpp
    src/scope_front_end.cpp
)
add_library(proton::instruments ALIAS proton_instruments)

target_include_directories(proton_instruments PUBLIC include)
target_link_libraries(proton_instruments PUBLIC proton::core PRIVATE proton_warnings)
