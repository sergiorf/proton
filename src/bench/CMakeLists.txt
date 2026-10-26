# SPDX-License-Identifier: AGPL-3.0-only
# The bench model and run control (ARCHITECTURE.md section 5.1): the Qt-free glue between engine,
# circuit and instruments, shared by the app, proton-spike and the tests.
add_library(proton_bench
    src/bench.cpp
    src/engines.cpp
    src/examples.cpp
    src/run_control.cpp
    src/sheet_bench.cpp
)
add_library(proton::bench ALIAS proton_bench)

target_include_directories(proton_bench PUBLIC include)
target_link_libraries(proton_bench
    PUBLIC proton::instruments proton::schematic proton::sim
    PRIVATE proton::core proton_warnings)
