# SPDX-License-Identifier: AGPL-3.0-only
add_library(proton_sim
    src/mock_engine.cpp
    src/real_time_pacer.cpp
    src/sim_chunk.cpp
    src/sim_worker.cpp
)
add_library(proton::sim ALIAS proton_sim)

target_include_directories(proton_sim PUBLIC include)
find_package(Threads REQUIRED)
# Public: SimWorker holds an SpscRing and a std::jthread.
target_link_libraries(proton_sim PUBLIC proton::core Threads::Threads PRIVATE proton_warnings)

# NgspiceEngine (M1.2) needs sharedspice.h; it loads ngspice.dll at runtime, so nothing links to it.
if(PROTON_NGSPICE_FOUND)
    target_sources(proton_sim PRIVATE src/ngspice_engine.cpp)
    target_link_libraries(proton_sim PRIVATE proton::ngspice_headers)
    # Public: code above src/sim offers ngspice only when this build has it.
    target_compile_definitions(proton_sim PUBLIC PROTON_HAVE_NGSPICE)
endif()
