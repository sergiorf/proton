# SPDX-License-Identifier: AGPL-3.0-only
add_library(proton_schematic
    src/circuit.cpp
    src/netlist.cpp
    src/units.cpp
    src/validate.cpp
)
add_library(proton::schematic ALIAS proton_schematic)

target_include_directories(proton_schematic PUBLIC include)
target_link_libraries(proton_schematic PRIVATE proton_warnings)
