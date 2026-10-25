# SPDX-License-Identifier: AGPL-3.0-only
# The Qt Widgets app (ARCHITECTURE.md section 11): the only target that links Qt. Built when
# PROTON_BUILD_APP is on, which is the default on Windows only (ADR 0003).
add_executable(proton WIN32
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
)
set_target_properties(proton PROPERTIES AUTOMOC ON)
target_link_libraries(proton PRIVATE proton::core Qt6::Widgets proton_warnings)

# Qt's DLLs and plugins next to proton.exe after every build, so it starts without Qt on PATH,
# from VS Code as from the smoke test. Dynamic linking keeps them replaceable (LGPL, LICENSING.md).
if(WIN32)
    add_custom_command(TARGET proton POST_BUILD
        COMMAND Qt6::windeployqt --verbose 0 --no-translations --no-system-d3d-compiler
            --no-system-dxc-compiler --no-opengl-sw --no-compiler-runtime
            --skip-plugin-types tls,networkinformation "$<TARGET_FILE:proton>"
        VERBATIM)
endif()

# The app opens its main window, closes it and exits 0.
add_test(NAME app_starts COMMAND proton --smoke-test)
