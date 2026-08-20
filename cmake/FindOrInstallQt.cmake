# A macro, deliberately, not a function - and the same goes for the postgresql
# one next door only by choice, whereas here it is a requirement.
#
# find_package() creates its targets globally, but records that it succeeded in
# ordinary variables: Qt6_FOUND, Qt6Core_FOUND, Qt6EntryPointPrivate_FOUND and a
# dozen more. Inside a function those die with the call, while the targets stay.
# The next find_package(Qt6 ...) - ours is in tests/CMakeLists.txt, asking for
# the Test component - then starts from scratch: find_dependency(Qt6Core) sees
# Qt6Core_FOUND unset and re-includes Qt6CoreConfig, which walks down to
# Qt6EntryPointPrivateConfig and its Qt6EntryPointMinGW32Target.cmake, where
#   add_library(EntryPointMinGW32 STATIC IMPORTED)
# is written with no `if(NOT TARGET ...)` guard. Second time round cmake stops
# with
#   _add_library cannot create imported target "EntryPointMinGW32" because
#   another target with the same name already exists.
# MinGW only, because that target file exists in no other Qt build - which is
# why this held together for so long.
#
# As a macro the find_package runs in the caller's scope (the top-level
# CMakeLists), so the variables live on, and the second call short-circuits the
# way it is meant to. Test is asked for here as an *optional* component for the
# same reason, one step further: with it, Qt is looked up exactly once in the
# whole project and tests/ needs no find_package of its own at all. Optional, so
# a Qt without the Test module still builds the application.
#
# The other half of why this has to be a macro: the CMAKE_PREFIX_PATH the
# fallbacks below extend has to be visible to the rest of the build too.
#
# Whether it is a macro is easy to check: `cmake --trace-expand` aside,
#   cmake -S . -B build 2>&1 | grep -c "Qt6 found via"
# stays 1, and tests/ reports Qt6::Test without a second lookup.
#
# Consequences of being a macro, both of which the body respects:
#   - no return(): it would return out of *CMakeLists.txt*, skipping the entire
#     rest of the build. Hence the if/else chain below.
#   - no local scope: every variable of our own is named _sqt_* so that nothing
#     unexpected is left behind in the caller.
macro(find_or_install_qt6)
    find_package(Qt6 QUIET COMPONENTS Widgets Qml OPTIONAL_COMPONENTS Test)

    if(Qt6_FOUND)
        message(STATUS "Qt6 found via standard search")
    elseif(WIN32)
        # Perhaps vcpkg has it. Not installed on demand: qt6-base is an hours
        # long build, and a Qt to point at is a reasonable thing to ask for.
        find_program(_sqt_vcpkg_exe vcpkg)
        if(_sqt_vcpkg_exe)
            get_filename_component(_sqt_vcpkg_dir "${_sqt_vcpkg_exe}" DIRECTORY)
            if(DEFINED VCPKG_TARGET_TRIPLET)
                set(_sqt_triplet "${VCPKG_TARGET_TRIPLET}")
            elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
                set(_sqt_triplet "x64-mingw-dynamic")
            else()
                set(_sqt_triplet "x64-windows")
            endif()
            set(_sqt_qt_prefix "${_sqt_vcpkg_dir}/installed/${_sqt_triplet}")
            list(APPEND CMAKE_PREFIX_PATH "${_sqt_qt_prefix}")
            find_package(Qt6 QUIET COMPONENTS Widgets Qml OPTIONAL_COMPONENTS Test)
            if(Qt6_FOUND)
                message(STATUS "Qt6 found via vcpkg at ${_sqt_qt_prefix}")
            endif()
        endif()

        if(NOT Qt6_FOUND)
            message(FATAL_ERROR
                "Qt6 not found on Windows.\n"
                "Please install Qt6 manually or via vcpkg:\n"
                "  vcpkg install qt6-base qt6-declarative:${_sqt_triplet}\n"
                "Or set CMAKE_PREFIX_PATH to your Qt installation."
            )
        endif()
    else()
        # Linux: ask qmake where its Qt lives.
        find_program(_sqt_qmake_exe qmake)
        if(NOT _sqt_qmake_exe)
            file(GLOB _sqt_qt_dirs "/home/*/Qt/*/gcc_64" "/opt/Qt/*/gcc_64")
            foreach(_sqt_dir ${_sqt_qt_dirs})
                if(EXISTS "${_sqt_dir}/bin/qmake")
                    set(_sqt_qmake_exe "${_sqt_dir}/bin/qmake")
                    break()
                endif()
            endforeach()
        endif()

        if(_sqt_qmake_exe)
            execute_process(
                COMMAND ${_sqt_qmake_exe} -query QT_INSTALL_PREFIX
                OUTPUT_VARIABLE _sqt_qt_prefix
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(_sqt_qt_prefix)
                message(STATUS "Found Qt via qmake at: ${_sqt_qt_prefix}")
                list(APPEND CMAKE_PREFIX_PATH "${_sqt_qt_prefix}")
                find_package(Qt6 REQUIRED COMPONENTS Widgets Qml OPTIONAL_COMPONENTS Test)
            endif()
        endif()

        if(NOT Qt6_FOUND)
            message(FATAL_ERROR
                "Qt6 not found on Linux.\n"
                "Install: sudo apt install qt6-base-dev qt6-declarative-dev\n"
                "Or set CMAKE_PREFIX_PATH to your Qt installation."
            )
        endif()
    endif()

    unset(_sqt_triplet)
    unset(_sqt_qt_prefix)
    unset(_sqt_qt_dirs)
    unset(_sqt_dir)
endmacro()
