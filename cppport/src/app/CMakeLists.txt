# The application services library (Machine, AppSettings, NotificationCenter,
# Jogger, ToolpathScene, Shortcuts, ConsoleLog, Diagnostics).

add_library(gs_app STATIC
    accessory_wizards.cpp
    accessory_wizards.hpp
    app_settings.cpp
    app_settings.hpp
    console_log.cpp
    console_log.hpp
    diagnostics.cpp
    diagnostics.hpp
    gcode_highlighter.cpp
    gcode_highlighter.hpp
    jogger.cpp
    jogger.hpp
    machine.cpp
    machine.hpp
    notification_center.cpp
    notification_center.hpp
    power.cpp
    power.hpp
    qt_event_loop.cpp
    qt_event_loop.hpp
    rotary_actions.cpp
    rotary_actions.hpp
    sd_card_utils.cpp
    sd_card_utils.hpp
    shortcuts.cpp
    shortcuts.hpp
)
target_include_directories(gs_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gs_app
    PUBLIC
        gs::compile_options
        gs::core
        gs::transport
        Qt6::Core
        Qt6::Gui)
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
    <QtCore> <QtGui>
    <algorithm> <functional> <memory> <string> <vector>)
