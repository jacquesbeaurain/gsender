# Copies the DLLs of imported shared libraries next to an executable so it
# runs straight from the build tree (tests, the app, the debugger).

set(GS_COPY_DLLS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/GsCopyDlls.cmake" CACHE INTERNAL "")

function(gs_copy_runtime_dlls target)
    if(NOT WIN32)
        return()
    endif()
    # The list is passed as a single argument; the script tolerates an empty
    # list, which `cmake -E copy_if_different` does not.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DGS_DLLS=$<TARGET_RUNTIME_DLLS:${target}>"
            "-DGS_DEST=$<TARGET_FILE_DIR:${target}>"
            -P "${GS_COPY_DLLS_SCRIPT}"
        VERBATIM)
endfunction()
