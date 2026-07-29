# Makes DST point at SRC using a directory symlink when the platform allows it,
# and falls back to a plain copy otherwise (Windows without Developer Mode,
# filesystems without symlink support, ...).
#
# Run at build time via `cmake -P`. Expected variables (pass with -D):
#   SRC        - existing source directory
#   DST        - destination path; must live inside BINARY_DIR
#   BINARY_DIR - build tree root, used to guard the recursive removal below
#   FORCE_COPY - optional; when true, skip the symlink attempt entirely

foreach(_var SRC DST BINARY_DIR)
    if(NOT DEFINED ${_var})
        message(FATAL_ERROR "LinkOrCopyDir: ${_var} is not set")
    endif()
endforeach()

# ABSOLUTE (unlike REALPATH) keeps symlinks in the paths themselves intact
get_filename_component(SRC "${SRC}" ABSOLUTE)
get_filename_component(DST "${DST}" ABSOLUTE)
get_filename_component(BINARY_DIR "${BINARY_DIR}" ABSOLUTE)

if(NOT IS_DIRECTORY "${SRC}")
    message(FATAL_ERROR "LinkOrCopyDir: '${SRC}' is not a directory")
endif()

set(_copy_only FALSE)
if(FORCE_COPY)
    set(_copy_only TRUE)
endif()

if(NOT _copy_only)
    if(IS_SYMLINK "${DST}")
        file(READ_SYMLINK "${DST}" _target)
        if(NOT IS_ABSOLUTE "${_target}")
            get_filename_component(_dst_dir "${DST}" DIRECTORY)
            get_filename_component(_target "${_dst_dir}/${_target}" ABSOLUTE)
        endif()
        if(_target STREQUAL SRC)
            return()  # already up to date, nothing to do
        endif()
        file(REMOVE "${DST}")  # drops the link, not the tree behind it
    elseif(EXISTS "${DST}")
        # Leftover from an earlier copy-based build. Only ever delete inside
        # the build tree.
        string(FIND "${DST}" "${BINARY_DIR}/" _inside)
        if(NOT _inside EQUAL 0)
            message(FATAL_ERROR
                "LinkOrCopyDir: refusing to remove '${DST}', it is outside of '${BINARY_DIR}'")
        endif()
        file(REMOVE_RECURSE "${DST}")
    endif()

    get_filename_component(_dst_parent "${DST}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dst_parent}")

    file(CREATE_LINK "${SRC}" "${DST}" SYMBOLIC RESULT _result)
    if(_result STREQUAL "0")
        return()
    endif()
    message(STATUS "LinkOrCopyDir: cannot symlink '${DST}' (${_result}); copying instead")
endif()

file(MAKE_DIRECTORY "${DST}")
file(COPY "${SRC}/" DESTINATION "${DST}")
