# Makes the Windows dependencies discoverable by find_package().
#
#   GS_LIBS_DIR  root holding boost_*, googletest-* and SDL3-* (D:\repos\libs)
#   GS_QT_DIR    Qt kit of the official install (D:\Qt\6.11.1\msvc2022_64)
#
# Each is a cache variable, or taken from the environment variable of the same
# name. Nothing is downloaded: a missing library is a configure error.
# Debug builds use the Debug builds of Boost (-gd), GoogleTest and Qt (d).

if(NOT WIN32)
    return()
endif()

foreach(_gs_var GS_LIBS_DIR GS_QT_DIR)
    if(NOT ${_gs_var} AND DEFINED ENV{${_gs_var}})
        set(${_gs_var} "$ENV{${_gs_var}}" CACHE PATH "" FORCE)
    endif()
    set(${_gs_var} "${${_gs_var}}" CACHE PATH "")
endforeach()

if(NOT GS_LIBS_DIR OR NOT GS_QT_DIR)
    message(STATUS "GS_LIBS_DIR / GS_QT_DIR not set; relying on the default package search")
    return()
endif()
file(TO_CMAKE_PATH "${GS_LIBS_DIR}" GS_LIBS_DIR)
file(TO_CMAKE_PATH "${GS_QT_DIR}" GS_QT_DIR)

# The newest match of a glob, or empty.
function(_gs_newest out)
    file(GLOB found LIST_DIRECTORIES TRUE ${ARGN})
    list(SORT found COMPARE NATURAL ORDER DESCENDING)
    list(GET found 0 first)
    set(${out} "${first}" PARENT_SCOPE)
endfunction()

_gs_newest(_gs_boost "${GS_LIBS_DIR}/boost_*/lib64-msvc-*/cmake/Boost-*")
_gs_newest(_gs_gtest "${GS_LIBS_DIR}/googletest-*")
_gs_newest(_gs_sdl3 "${GS_LIBS_DIR}/SDL3-*")

if(NOT _gs_boost OR NOT EXISTS "${_gs_boost}/BoostConfig.cmake")
    message(FATAL_ERROR "No Boost (boost_*/lib64-msvc-*/cmake/Boost-*) in GS_LIBS_DIR='${GS_LIBS_DIR}'")
endif()
if(GS_BUILD_TESTS AND (NOT _gs_gtest OR NOT EXISTS "${_gs_gtest}/lib/gtest.lib"))
    message(FATAL_ERROR "No GoogleTest (googletest-*/lib/gtest.lib) in GS_LIBS_DIR='${GS_LIBS_DIR}'")
endif()

set(Boost_DIR "${_gs_boost}" CACHE PATH "" FORCE)
# That GoogleTest is a build tree (headers in the source dirs, libs in lib/),
# whose exported config points at an include dir that does not exist: name
# the targets here instead of find_package(GTest).
if(GS_BUILD_TESTS)
    foreach(_gs_lib gtest gtest_main)
        add_library(GTest::${_gs_lib} STATIC IMPORTED GLOBAL)
        set_target_properties(GTest::${_gs_lib} PROPERTIES
            IMPORTED_LOCATION "${_gs_gtest}/lib/${_gs_lib}.lib"
            IMPORTED_LOCATION_DEBUG "${_gs_gtest}/build/lib/Debug/${_gs_lib}.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${_gs_gtest}/googletest/include")
    endforeach()
    target_link_libraries(GTest::gtest_main INTERFACE GTest::gtest)
endif()
if(_gs_sdl3)
    set(SDL3_DIR "${_gs_sdl3}/cmake" CACHE PATH "" FORCE)
endif()
list(PREPEND CMAKE_PREFIX_PATH "${GS_QT_DIR}")
set(GS_QT_BIN_DIR "${GS_QT_DIR}/bin" CACHE INTERNAL "")
message(STATUS "Windows dependencies: Qt ${GS_QT_DIR}; Boost ${_gs_boost}; GTest ${_gs_gtest}; SDL3 ${_gs_sdl3}")

# Release packages (Boost, SDL3) may only provide the RELEASE imported configuration; let
# RelWithDebInfo/MinSizeRel builds consume them.
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release RelWithDebInfo)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release MinSizeRel)
