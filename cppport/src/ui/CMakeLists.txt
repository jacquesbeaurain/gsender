# The QML touch UI: a QML module (URI GSender) over the application's
# services (gs_app's Machine and friends), and its executable. Being ported
# screen by screen beside the widget application (DEV_WALKTHROUGH.md, Step 61).

qt_policy(SET QTP0001 NEW)  # modules under :/qt/qml
qt_policy(SET QTP0004 NEW)  # qmldir files per directory
set(CMAKE_AUTOMOC ON)

# Must precede qt_add_qml_module, which writes the qmldir.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(gs_ui STATIC)
qt_add_qml_module(gs_ui
    URI GSender
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/GSender"
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Icon.qml
        qml/Card.qml
        qml/TopBar.qml
        qml/ConnectionButton.qml
        qml/StatusPill.qml
        qml/NavRail.qml
        qml/NavButton.qml
        qml/CarvePage.qml
        qml/Visualizer.qml
        qml/ToolpathGestures.qml
        qml/PlaceholderPage.qml
        qml/GButton.qml
        qml/ConfirmDialog.qml
        qml/NumberField.qml
        qml/CornerButton.qml
        qml/GoToPopup.qml
        qml/DroPanel.qml
        qml/JogWheel.qml
        qml/TabJog.qml
        qml/JogPanel.qml
        qml/LocationColumn.qml
        qml/WorkspaceSelector.qml
        qml/FileControl.qml
        qml/OverrideSlider.qml
        qml/StartFromLinePopup.qml
        qml/JobControl.qml
        qml/ProgressArea.qml
        qml/MachineInfoPopup.qml
        qml/ToastArea.qml
        qml/NotificationBell.qml
        qml/HelperPanel.qml
        qml/JobAlerts.qml
        qml/ToolTab.qml
        qml/ToolsWidget.qml
        qml/ConsoleTab.qml
        qml/ActiveStateButton.qml
        qml/GSwitch.qml
        qml/CoolantTab.qml
        qml/SpindleTab.qml
        qml/MacroForm.qml
        qml/MacrosTab.qml
        qml/ProbeTab.qml
        qml/RunProbeDialog.qml
        qml/RotaryTab.qml
        qml/MountingSetupDialog.qml
        qml/ToolChange.qml
        qml/StepThrough.qml
        qml/GcodeEditor.qml
        qml/PieChart.qml
        qml/StatCard.qml
        qml/CardHeader.qml
        qml/MaintenanceReminder.qml
        qml/DiagnosticCard.qml
        qml/StatsOverview.qml
        qml/StatsJobs.qml
        qml/StatsMaintenance.qml
        qml/MaintenanceTaskDialog.qml
        qml/StatsAlarms.qml
        qml/StatsAbout.qml
        qml/StatsPage.qml
    SOURCES
        backend.cpp
        backend.hpp
        connection_model.cpp
        connection_model.hpp
        status_model.cpp
        status_model.hpp
        file_model.cpp
        file_model.hpp
        job_model.cpp
        job_model.hpp
        dro_model.cpp
        dro_model.hpp
        jog_model.cpp
        jog_model.hpp
        console_model.cpp
        console_model.hpp
        spindle_model.cpp
        spindle_model.hpp
        macros_model.cpp
        macros_model.hpp
        probe_model.cpp
        probe_model.hpp
        rotary_model.cpp
        rotary_model.hpp
        tool_change_model.cpp
        tool_change_model.hpp
        step_through_model.cpp
        step_through_model.hpp
        gcode_editor_model.cpp
        gcode_editor_model.hpp
        stats_model.cpp
        stats_model.hpp
        icon_provider.cpp
        icon_provider.hpp
        toolpath_item.cpp
        toolpath_item.hpp
        ui_app.cpp
        ui_shortcuts.cpp
        ui_shortcuts.hpp
        ui_app.hpp
)

# Upstream's pictures (the probe routines' GIFs, the rotary tracks).
file(GLOB_RECURSE GS_IMAGE_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/images/*")
qt_add_resources(gs_ui images
    PREFIX "/images"
    BASE "${PROJECT_SOURCE_DIR}/resources/images"
    FILES ${GS_IMAGE_FILES})

# The icons (tools/extract_icons.mjs), for the icon provider.
file(GLOB GS_ICON_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/icons/*.svg")
qt_add_resources(gs_ui icons
    PREFIX "/icons"
    BASE "${PROJECT_SOURCE_DIR}/resources/icons"
    FILES ${GS_ICON_FILES})

target_include_directories(gs_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gs_ui
    PUBLIC
        gs_app
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickDialogs2
        Qt6::Svg)
# Where Qt's own QML modules are, for runs from the build tree (ui_app.cpp).
if(DEFINED QT6_INSTALL_PREFIX AND DEFINED QT6_INSTALL_QML)
    target_compile_definitions(gs_ui PRIVATE GS_QT_QML_DIR="${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}")
endif()

qt_add_executable(gsender-qml WIN32 main.cpp)
target_link_libraries(gsender-qml PRIVATE gs_ui gs_uiplugin)
gs_copy_runtime_dlls(gsender-qml)
gs_deploy_qt_plugins(gsender-qml)
