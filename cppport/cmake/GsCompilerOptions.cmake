# Shared compile settings. Every first-party target links gs::compile_options.

add_library(gs_compile_options INTERFACE)
add_library(gs::compile_options ALIAS gs_compile_options)

target_compile_features(gs_compile_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(gs_compile_options INTERFACE
        /W4
        /permissive-
        /utf-8
        /Zc:__cplusplus
        /Zc:inline
        /EHsc
        /bigobj
        # Third-party headers are consumed through imported targets and
        # treated as external; keep their warnings out of our builds.
        /external:W0)
    target_compile_definitions(gs_compile_options INTERFACE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0A00
        _CRT_SECURE_NO_WARNINGS
        # CMake targets handle linking; never let Boost auto-link by pragma.
        BOOST_ALL_NO_LIB)
    if(GS_WARNINGS_AS_ERRORS)
        target_compile_options(gs_compile_options INTERFACE /WX)
    endif()
else()
    target_compile_options(gs_compile_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
    if(GS_WARNINGS_AS_ERRORS)
        target_compile_options(gs_compile_options INTERFACE -Werror)
    endif()
endif()
