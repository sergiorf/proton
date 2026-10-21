# SPDX-License-Identifier: AGPL-3.0-only
add_library(proton_sim
    src/mock_engine.cpp
    src/sim_chunk.cpp
)
add_library(proton::sim ALIAS proton_sim)

target_include_directories(proton_sim PUBLIC include)
target_link_libraries(proton_sim PRIVATE proton_warnings)
