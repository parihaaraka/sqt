# Works out the version to build, in two forms:
#
#   SQT_VERSION      x.y.z  - numeric only, what project(... VERSION) and the
#                             package managers accept
#   SQT_VERSION_STR  the full description, which for anything but a release
#                    build carries the distance from the tag and the commit
#                    ("0.5.0-12-g3277284"). This is what the about box shows,
#                    so a bug report says which build it came from.
#
# Three sources, in order:
#   1. -DSQT_VERSION=x.y.z on the command line. The release workflow passes the
#      tag this way, so the tag is the single source of truth there.
#   2. `git describe` in a checkout with tags.
#   3. SQT_VERSION_FALLBACK below, for a tarball without git.
#
# Must be included *before* project().

set(SQT_VERSION_FALLBACK "0.5.0")

function(_sqt_numeric_version text out_var)
    # Keeps the leading x[.y[.z]] and drops whatever a tag description appends.
    if(text MATCHES "^v?([0-9]+(\\.[0-9]+)*)")
        set(${out_var} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

if(DEFINED SQT_VERSION AND NOT SQT_VERSION STREQUAL "")
    set(_sqt_raw "${SQT_VERSION}")
    set(_sqt_origin "command line")
else()
    set(_sqt_raw "")
    # find_program rather than find_package(Git): this runs before project(),
    # where the module-based finders are not all on their feet yet.
    find_program(GIT_EXECUTABLE NAMES git)
    if(GIT_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*" --dirty
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _sqt_raw
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        set(_sqt_origin "git describe")
    endif()
    if(_sqt_raw STREQUAL "")
        set(_sqt_raw "${SQT_VERSION_FALLBACK}")
        set(_sqt_origin "fallback")
    endif()
endif()

string(REGEX REPLACE "^v" "" SQT_VERSION_STR "${_sqt_raw}")
_sqt_numeric_version("${SQT_VERSION_STR}" SQT_VERSION)

if(SQT_VERSION STREQUAL "")
    message(FATAL_ERROR
        "SqtVersion: cannot read a version out of '${_sqt_raw}' (${_sqt_origin}). "
        "Expected something like 1.2.3 or v1.2.3.")
endif()

message(STATUS "sqt version: ${SQT_VERSION_STR} (${_sqt_origin})")
