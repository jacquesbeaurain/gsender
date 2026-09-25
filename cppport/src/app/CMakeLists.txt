# The Qt Widgets application. Its code lives in a library so tests can link it.

add_library(gs_app STATIC
    accessibility.cpp
    accessibility.hpp
    accessory_installer.cpp
    accessory_installer.hpp
    accessory_wizards.cpp
    accessory_wizards.hpp
    app_settings.cpp
    app_settings.hpp
    appearance.cpp
    appearance.hpp
    calibration_dialogs.cpp
    calibration_dialogs.hpp
    console_log.cpp
    console_log.hpp
    console_panel.cpp
    console_panel.hpp
    controls.cpp
    controls.hpp
    diagnostics.cpp
    diagnostics.hpp
    dro_panel.cpp
    dro_panel.hpp
    gcode_editor_dialog.cpp
    gcode_editor_dialog.hpp
    gcode_highlighter.cpp
    gcode_highlighter.hpp
    helper_info.cpp
    helper_info.hpp
    machine.cpp
    machine.hpp
    jogger.cpp
    jogger.hpp
    main_window.cpp
    main_window.hpp
    notifications.cpp
    notifications.hpp
    panels.cpp
    panels.hpp
    pie_chart.cpp
    pie_chart.hpp
    power.cpp
    power.hpp
    probe_panel.cpp
    probe_panel.hpp
    qt_event_loop.cpp
    qt_event_loop.hpp
    rotary_panel.cpp
    rotary_panel.hpp
    sd_card_dialog.cpp
    sd_card_dialog.hpp
    settings_dialog.cpp
    settings_dialog.hpp
    shortcuts.cpp
    shortcuts.hpp
    shortcuts_dialog.cpp
    shortcuts_dialog.hpp
    start_from_line_dialog.cpp
    start_from_line_dialog.hpp
    step_through_dialog.cpp
    step_through_dialog.hpp
    stats_dialog.cpp
    stats_dialog.hpp
    status_area.cpp
    status_area.hpp
    surfacing_dialog.cpp
    surfacing_dialog.hpp
    toolchange_dialog.cpp
    toolchange_dialog.hpp
    toolpath_view.cpp
    toolpath_view.hpp
    toolpath_scene.cpp
    toolpath_scene.hpp
    visualizer_theme.cpp
    visualizer_theme.hpp
)
target_include_directories(gs_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gs_app
    PUBLIC
        gs::compile_options
        gs::core
        gs::transport
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets)
if(WIN32)
    target_link_libraries(gs_app PRIVATE winmm)  # PlaySound: the audio cues
endif()
set_target_properties(gs_app PROPERTIES AUTOMOC ON)
# The accessory wizards' pictures and programs (tools/extract_data.mjs),
# compiled by rcc: too large for the hex embedding the core uses.
file(GLOB GS_ACCESSORY_FILES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/resources/accessories/*.png"
    "${PROJECT_SOURCE_DIR}/resources/accessories/*.gcode")
qt_add_resources(gs_app accessory_assets
    PREFIX "/accessories"
    BASE "${PROJECT_SOURCE_DIR}/resources/accessories"
    FILES ${GS_ACCESSORY_FILES})
# Stats > About: the release notes, the logo (also the window icon), the flag.
qt_add_resources(gs_app about_assets
    PREFIX "/about"
    BASE "${PROJECT_SOURCE_DIR}/resources/about"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/about/notes.json"
        "${PROJECT_SOURCE_DIR}/resources/about/icon-square.png"
        "${PROJECT_SOURCE_DIR}/resources/about/canada-flag-icon.png")
# Qt's emit/signals/slots macros would collide with plain C++ (and Boost)
# identifiers in the core headers: use Q_EMIT / Q_SIGNALS / Q_SLOTS.
target_compile_definitions(gs_app PUBLIC QT_NO_KEYWORDS)
gs_precompile_headers(gs_app
    <QtCore> <QtGui> <QtWidgets>
    <algorithm> <functional> <memory> <string> <vector>)

add_executable(gsender WIN32 main.cpp)
target_link_libraries(gsender PRIVATE gs_app)
gs_copy_runtime_dlls(gsender)
gs_deploy_qt_plugins(gsender)
