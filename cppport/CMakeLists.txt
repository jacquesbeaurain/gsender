cmake_minimum_required(VERSION 3.25)

# Embedded (/Z7) debug information: every object carries its own, so parallel
# compiles do not serialize on a shared PDB (mspdbsrv). Must precede project().
if(NOT DEFINED CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()

project(gSenderCpp
    VERSION 0.1.0
    DESCRIPTION "Native C++/Qt port of the gSender CNC sender"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Everything that produces an executable or DLL lands in one directory, so
# runtime dependencies copied next to one binary serve all of them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(GS_BUILD_TESTS "Build the unit test suites" ON)
option(GS_BUILD_APP "Build the Qt desktop application" ON)
option(GS_BUILD_QML "Build the QML touch UI (needs Qt Quick)" ON)
option(GS_DEPLOY_QT "Deploy Qt dependencies with windeployqt (Windows)" ON)
option(GS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(GS_USE_PCH "Precompile common standard and third-party headers" ON)
option(GS_WITH_SDL3 "Read gamepads with SDL3 (built without: no gamepads)" ON)

include(GsWindowsDeps)
include(GsCompilerOptions)
include(GsRuntimeDeps)
include(GsEmbed)

# Boost: headers (Asio, Regex) plus the compiled JSON library.
find_package(Boost 1.91 CONFIG REQUIRED COMPONENTS json)

add_subdirectory(src/core)
add_subdirectory(src/transport)

if(GS_BUILD_APP AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/CMakeLists.txt")
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network)
    # Gamepads: SDL3. Windows: the SDL3 package in GS_LIBS_DIR
    # (GsWindowsDeps.cmake); Linux: the conda prefix (tools/setup_linux.sh).
    if(GS_WITH_SDL3)
        find_package(SDL3 3.2 CONFIG COMPONENTS SDL3-shared)
        if(NOT SDL3_FOUND)
            message(STATUS "SDL3 not found: built without gamepads (Windows: SDL3-* in GS_LIBS_DIR)")
        elseif(TARGET SDL3::SDL3-shared)
            # The VC package sets only config-less locations, which the
            # config mapping (GsWindowsDeps.cmake: RelWithDebInfo ->
            # Release) never falls back to: name them for Release too.
            get_target_property(_gs_sdl3_dll SDL3::SDL3-shared IMPORTED_LOCATION)
            get_target_property(_gs_sdl3_lib SDL3::SDL3-shared IMPORTED_IMPLIB)
            if(_gs_sdl3_dll AND _gs_sdl3_lib)
                set_target_properties(SDL3::SDL3-shared PROPERTIES
                    IMPORTED_LOCATION_RELEASE "${_gs_sdl3_dll}"
                    IMPORTED_IMPLIB_RELEASE "${_gs_sdl3_lib}")
            endif()
        endif()
    endif()
    add_subdirectory(src/app)
    # The QML touch UI, where the Qt has Qt Quick and its Controls.
    if(GS_BUILD_QML)
        find_package(Qt6 6.5 COMPONENTS Qml Quick QuickControls2 QuickDialogs2 Svg)
        if(Qt6Quick_FOUND AND Qt6QuickControls2_FOUND AND Qt6QuickDialogs2_FOUND AND Qt6Svg_FOUND)
            add_subdirectory(src/ui)
        else()
            message(STATUS "Qt Quick, Quick Controls 2, Quick Dialogs 2 or Svg missing: the QML UI (gsender) is not built")
        endif()
    endif()
endif()

if(GS_BUILD_TESTS)
    enable_testing()
    if(NOT TARGET GTest::gtest)
        find_package(GTest CONFIG REQUIRED)
    endif()
    include(GoogleTest)
    add_subdirectory(tests)
endif()
