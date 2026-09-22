# Makes the packages of a FreeCAD LibPack discoverable by find_package().
#
# The LibPack is a prebuilt dependency bundle (Qt, Boost, GTest, ...). Debug and
# Release builds need different LibPacks because the Debug one is built against
# the debug CRT (/MDd); mixing the two produces link errors or heap corruption.
#
# Set GS_LIBPACK_DIR (cache variable or environment variable) to the LibPack root.

set(GS_LIBPACK_DIR "" CACHE PATH
    "Root of a FreeCAD LibPack (use the Debug LibPack for Debug builds)")

if(NOT GS_LIBPACK_DIR AND DEFINED ENV{GS_LIBPACK_DIR})
    set(GS_LIBPACK_DIR "$ENV{GS_LIBPACK_DIR}" CACHE PATH
        "Root of a FreeCAD LibPack (use the Debug LibPack for Debug builds)" FORCE)
endif()

if(NOT GS_LIBPACK_DIR)
    message(STATUS "GS_LIBPACK_DIR not set; relying on the default package search")
    return()
endif()

file(TO_CMAKE_PATH "${GS_LIBPACK_DIR}" GS_LIBPACK_DIR)

if(NOT EXISTS "${GS_LIBPACK_DIR}/FREECAD_LIBPACK_VERSION")
    message(FATAL_ERROR
        "GS_LIBPACK_DIR='${GS_LIBPACK_DIR}' does not look like a FreeCAD LibPack "
        "(missing FREECAD_LIBPACK_VERSION)")
endif()

file(READ "${GS_LIBPACK_DIR}/FREECAD_LIBPACK_VERSION" _gs_libpack_version)
string(STRIP "${_gs_libpack_version}" _gs_libpack_version)
message(STATUS "Using FreeCAD LibPack ${_gs_libpack_version}: ${GS_LIBPACK_DIR}")

list(PREPEND CMAKE_PREFIX_PATH "${GS_LIBPACK_DIR}" "${GS_LIBPACK_DIR}/lib/cmake")
set(GS_LIBPACK_BIN_DIR "${GS_LIBPACK_DIR}/bin" CACHE INTERNAL "")
set(GS_LIBPACK_PLUGIN_DIR "${GS_LIBPACK_DIR}/plugins" CACHE INTERNAL "")

# The debug LibPack ships debug-suffixed Qt DLLs; use that to catch a
# configuration mismatch early instead of at link or run time.
if(EXISTS "${GS_LIBPACK_DIR}/bin/Qt6Cored.dll")
    set(_gs_libpack_is_debug TRUE)
else()
    set(_gs_libpack_is_debug FALSE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT _gs_libpack_is_debug)
    message(WARNING "Debug build configured against a Release LibPack")
elseif(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND _gs_libpack_is_debug)
    message(WARNING "${CMAKE_BUILD_TYPE} build configured against a Debug LibPack")
endif()

# A Release LibPack only provides the RELEASE imported configuration; let
# RelWithDebInfo/MinSizeRel builds consume it.
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release RelWithDebInfo)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release MinSizeRel)
