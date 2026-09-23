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

# Qt loads its platform plugin (and styles) from <exe dir>/platforms and
# <exe dir>/styles when run from the build tree. Debug builds use the Debug
# LibPack's "d"-suffixed plugins.
function(gs_deploy_qt_plugins target)
    if(NOT WIN32 OR NOT GS_LIBPACK_DIR)
        return()
    endif()
    set(plugins "${GS_LIBPACK_DIR}/plugins")
    set(d "$<$<CONFIG:Debug>:d>")
    set(dir "$<TARGET_FILE_DIR:${target}>")
    # Qt's own DLL dependencies are not imported targets, so
    # $<TARGET_RUNTIME_DLLS> misses them (found with dumpbin /dependents).
    set(bin "${GS_LIBPACK_DIR}/bin")
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${bin}/libpng16${d}.dll" "${bin}/z${d}.dll" "${bin}/zstd.dll"
            "${dir}"
        VERBATIM)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}/platforms" "${dir}/styles"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${plugins}/platforms/qwindows${d}.dll"
            "${plugins}/platforms/qoffscreen${d}.dll"
            "${dir}/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${plugins}/styles/qmodernwindowsstyle${d}.dll"
            "${dir}/styles"
        VERBATIM)
endfunction()
