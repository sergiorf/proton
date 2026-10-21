# SPDX-License-Identifier: AGPL-3.0-only
# The M0 pipeline as a library, shared by proton-spike and tests/integration.
add_library(proton_spike_pipeline
    src/pipeline.cpp
)
add_library(proton::spike_pipeline ALIAS proton_spike_pipeline)

target_include_directories(proton_spike_pipeline PUBLIC include)
target_link_libraries(proton_spike_pipeline
    PUBLIC proton::schematic proton::instruments proton::sim
    PRIVATE proton::core proton_warnings)

add_executable(proton-spike
    src/main.cpp
)
target_link_libraries(proton-spike PRIVATE proton::spike_pipeline proton::core proton_warnings)
