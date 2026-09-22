# Compiles data files (JSON tables, profiles, ...) into a target so the
# libraries have no runtime file dependencies. Look them up with
# gs::resources::find("<path relative to BASE_DIR>").
#
#   gs_embed_resources(<target> BASE_DIR <dir> FILES <file>...)

set(GS_EMBED_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/GsEmbedScript.cmake" CACHE INTERNAL "")

function(gs_embed_resources target)
    cmake_parse_arguments(ARG "" "BASE_DIR" "FILES" ${ARGN})
    if(NOT ARG_BASE_DIR)
        message(FATAL_ERROR "gs_embed_resources: BASE_DIR is required")
    endif()

    set(_abs_files "")
    foreach(_file IN LISTS ARG_FILES)
        get_filename_component(_abs "${_file}" ABSOLUTE BASE_DIR "${ARG_BASE_DIR}")
        list(APPEND _abs_files "${_abs}")
    endforeach()

    set(_out "${CMAKE_CURRENT_BINARY_DIR}/${target}_embedded_resources.cpp")
    string(REPLACE ";" "|" _file_arg "${_abs_files}")
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND}
            "-DGS_OUT=${_out}"
            "-DGS_BASE=${ARG_BASE_DIR}"
            "-DGS_FILES=${_file_arg}"
            -P "${GS_EMBED_SCRIPT}"
        DEPENDS ${_abs_files} "${GS_EMBED_SCRIPT}"
        COMMENT "Embedding resources into ${target}"
        VERBATIM)
    target_sources(${target} PRIVATE "${_out}")
endfunction()
