# SPDX-License-Identifier: AGPL-3.0-only
# The Qt Widgets app (ARCHITECTURE.md section 11): the only target that links Qt. Built when
# PROTON_BUILD_APP is on, which is the default on Windows only (ADR 0003).
add_executable(proton WIN32
    src/generator_panel.cpp
    src/generator_panel.h
    src/icons.cpp
    src/icons.h
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    src/multimeter_panel.cpp
    src/multimeter_panel.h
    src/panel/knob_widget.cpp
    src/panel/knob_widget.h
    src/panel/lit_button.cpp
    src/panel/lit_button.h
    src/panel/panel_section.cpp
    src/panel/panel_section.h
    src/panel/panel_style.h
    src/panel/segment_display.cpp
    src/panel/segment_display.h
    src/panel/selector_widget.cpp
    src/panel/selector_widget.h
    src/schematic/parts_panel.cpp
    src/schematic/parts_panel.h
    src/schematic/schematic_view.cpp
    src/schematic/schematic_view.h
    src/schematic/symbols.cpp
    src/schematic/symbols.h
    src/scope_panel.cpp
    src/scope_panel.h
    src/scope_widget.cpp
    src/scope_widget.h
    src/smoke.cpp
    src/smoke.h
    src/start_page.cpp
    src/start_page.h
    resources/resources.qrc
)
# The app's icon: proton.png for the window (Qt resource), proton.ico for the executable.
if(WIN32)
    target_sources(proton PRIVATE resources/proton.rc)
endif()
set_target_properties(proton PROPERTIES AUTOMOC ON AUTORCC ON)
# Qt's `signals`, `slots` and `emit` macros would clash with names in the Qt-free libraries
# (SimChunk::signals): the app writes Q_SIGNALS and Q_EMIT instead.
target_compile_definitions(proton PRIVATE QT_NO_KEYWORDS)
target_link_libraries(proton
    PRIVATE proton::bench proton::project proton::core Qt6::Widgets proton_warnings)
# The examples run on ngspice, loaded from the executable's directory.
if(PROTON_NGSPICE_FOUND)
    proton_copy_ngspice_dlls(proton)
endif()
# The example projects (PLAN.md M4.11) in examples/ next to proton.exe, where the app looks for
# them, after every build.
add_custom_command(TARGET proton POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/examples"
        "$<TARGET_FILE_DIR:proton>/examples"
    VERBATIM)

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
# Every edit on the sheet undoes back to the empty sheet and redoes to the same scene (M4.3).
add_test(NAME app_undo_redo COMMAND proton --smoke-undo)
# Each drawing mistake is marked on the sheet where it is, its message on hover (M4.9).
add_test(NAME app_editor_diagnostics COMMAND proton --smoke-diagnostics)
# Box selection, Escape and every entry of the sheet's right-click menu (M4.10).
add_test(NAME app_selection COMMAND proton --smoke-selection)
# Projects save, open and reopen through the menus, the toolbar and the start page (M4.11).
add_test(NAME app_project_files COMMAND proton --smoke-files)
# Every example runs in the app, one after the other, as when chosen from the menu (M2.2), and
# every scope control answers wheel, keys, drag and clicks (M2.4).
if(PROTON_NGSPICE_FOUND)
    add_test(NAME app_runs_examples COMMAND proton --smoke-examples)
    add_test(NAME app_panel_controls COMMAND proton --smoke-panel)
    # Turning the generator's frequency reaches the scope in time (M2.6, ADR 0005).
    add_test(NAME app_generator_latency COMMAND proton --smoke-latency)
    # Parts and instruments placed from the parts panel and wired by hand measure their own nets,
    # two scopes and two multimeters at once (M4.2, M4.4).
    add_test(NAME app_draws_by_hand COMMAND proton --smoke-editor)
    # Every menu and toolbar action, Pause and Run on from it included (M4.7).
    add_test(NAME app_menus_and_toolbars COMMAND proton --smoke-menus)
    # A student finds the RC low-pass's corner with the panels: -3 dB and a 45 degree lag (M4.8).
    add_test(NAME app_corner_frequency_session COMMAND proton --smoke-session)
endif()
