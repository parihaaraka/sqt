function(find_or_install_qt6)
    find_package(Qt6 QUIET COMPONENTS Widgets Qml)
    if(Qt6_FOUND)
        message(STATUS "Qt6 found via standard search")
        return()
    endif()

    if(WIN32)
        # try to find via vcpkg (may be installed already)
        find_program(VCPKG_EXE vcpkg)
        if(VCPKG_EXE)
            get_filename_component(VCPKG_DIR "${VCPKG_EXE}" DIRECTORY)
            if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
                set(VCPKG_TRIPLET "x64-mingw-dynamic")
            else()
                set(VCPKG_TRIPLET "x64-windows")
            endif()
            set(VCPKG_INSTALLED "${VCPKG_DIR}/installed/${VCPKG_TRIPLET}")
            list(APPEND CMAKE_PREFIX_PATH "${VCPKG_INSTALLED}")
            find_package(Qt6 QUIET COMPONENTS Widgets Qml)
            if(Qt6_FOUND)
                message(STATUS "Qt6 found via vcpkg at ${VCPKG_INSTALLED}")
                return()
            endif()
        endif()

        message(FATAL_ERROR
            "Qt6 not found on Windows.\n"
            "Please install Qt6 manually or via vcpkg:\n"
            "  vcpkg install qt6-base qt6-declarative:${VCPKG_TRIPLET}\n"
            "Or set CMAKE_PREFIX_PATH to your Qt installation."
        )
    else()
        # Linux: search via qmake
        find_program(QMAKE_EXECUTABLE qmake)
        if(NOT QMAKE_EXECUTABLE)
            file(GLOB QT_INSTALL_DIRS "/home/*/Qt/*/gcc_64" "/opt/Qt/*/gcc_64")
            foreach(DIR ${QT_INSTALL_DIRS})
                if(EXISTS "${DIR}/bin/qmake")
                    set(QMAKE_EXECUTABLE "${DIR}/bin/qmake")
                    break()
                endif()
            endforeach()
        endif()

        if(QMAKE_EXECUTABLE)
            execute_process(
                COMMAND ${QMAKE_EXECUTABLE} -query QT_INSTALL_PREFIX
                OUTPUT_VARIABLE QT_INSTALL_PREFIX
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            if(QT_INSTALL_PREFIX)
                message(STATUS "Found Qt via qmake at: ${QT_INSTALL_PREFIX}")
                list(APPEND CMAKE_PREFIX_PATH ${QT_INSTALL_PREFIX})
                find_package(Qt6 REQUIRED COMPONENTS Widgets Qml)
                return()
            endif()
        endif()

        message(FATAL_ERROR
            "Qt6 not found on Linux.\n"
            "Install: sudo apt install qt6-base-dev qt6-declarative-dev\n"
            "Or set CMAKE_PREFIX_PATH to your Qt installation."
        )
    endif()
endfunction()
