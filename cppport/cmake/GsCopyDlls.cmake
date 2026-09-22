# Script mode helper for gs_copy_runtime_dlls(): copies each DLL in GS_DLLS to
# GS_DEST when it is missing or different.

foreach(_dll IN LISTS GS_DLLS)
    if(NOT _dll)
        continue()
    endif()
    get_filename_component(_name "${_dll}" NAME)
    file(COPY_FILE "${_dll}" "${GS_DEST}/${_name}" ONLY_IF_DIFFERENT)
endforeach()
