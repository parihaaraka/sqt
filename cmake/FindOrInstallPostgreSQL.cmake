include(DownloadVcpkg)

function(find_or_install_postgresql)
    find_package(PostgreSQL)
    if(PostgreSQL_FOUND)
        message(STATUS "PostgreSQL found: ${PostgreSQL_LIBRARIES}")
        set(PostgreSQL_INCLUDE_DIRS ${PostgreSQL_INCLUDE_DIRS} PARENT_SCOPE)
        set(PostgreSQL_LIBRARIES ${PostgreSQL_LIBRARIES} PARENT_SCOPE)
        return()
    endif()

    if(WIN32)
        download_vcpkg()

        # Determine triplet based on compiler
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(VCPKG_TRIPLET "x64-mingw-dynamic")
            set(VCPKG_HOST_TRIPLET "x64-mingw-dynamic")
            set(VCPKG_TRIPLET_FILE "${CMAKE_SOURCE_DIR}/vcpkg/triplets/${VCPKG_TRIPLET}.cmake")
            file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/vcpkg/triplets")
            file(WRITE "${VCPKG_TRIPLET_FILE}"
                "set(VCPKG_TARGET_IS_MINGW ON)\n"
                "set(VCPKG_LIBRARY_LINKAGE dynamic)\n"
                "set(VCPKG_CRT_LINKAGE dynamic)\n"
                "set(VCPKG_TARGET_ARCHITECTURE x64)\n"
                "set(VCPKG_CMAKE_SYSTEM_NAME MinGW)\n"
                "set(VCPKG_HOST_IS_MINGW ON)\n"
                "set(VCPKG_ENV_PASSTHROUGH PATH)\n"
            )
            set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_SOURCE_DIR}/vcpkg/triplets")
        else()
            set(VCPKG_TRIPLET "x64-windows")
            set(VCPKG_HOST_TRIPLET "x64-windows")
        endif()

        set(VCPKG_INSTALLED "${CMAKE_SOURCE_DIR}/vcpkg_installed/${VCPKG_TRIPLET}")
        file(MAKE_DIRECTORY "${VCPKG_INSTALLED}")

        # Install libpq if not already present
        if(NOT EXISTS "${VCPKG_INSTALLED}/include/libpq-fe.h" AND NOT EXISTS "${VCPKG_INSTALLED}/${VCPKG_TRIPLET}/include/libpq-fe.h")
            message(STATUS "Installing libpq via vcpkg (${VCPKG_TRIPLET})...")
            set(ENV{CC} ${CMAKE_C_COMPILER})
            set(ENV{CXX} ${CMAKE_CXX_COMPILER})
            execute_process(
                COMMAND "${VCPKG_EXE}" install libpq:${VCPKG_TRIPLET}
                    --host-triplet=${VCPKG_HOST_TRIPLET}
                    --overlay-triplets=${VCPKG_OVERLAY_TRIPLETS}
                    --x-install-root=${VCPKG_INSTALLED}
                WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                RESULT_VARIABLE INSTALL_RESULT
            )
            if(NOT INSTALL_RESULT EQUAL 0)
                message(FATAL_ERROR "vcpkg install libpq failed")
            endif()
        else()
            message(STATUS "libpq already installed at ${VCPKG_INSTALLED}")
        endif()

        # Manual search for headers and library (find_package does not work because vcpkg does not provide config files)
        find_path(PG_INCLUDE_DIR libpq-fe.h
            PATHS "${VCPKG_INSTALLED}"
            PATH_SUFFIXES include "${VCPKG_TRIPLET}/include"
            NO_DEFAULT_PATH
        )
        find_library(PG_LIBRARY NAMES pq libpq
            PATHS "${VCPKG_INSTALLED}"
            PATH_SUFFIXES lib "${VCPKG_TRIPLET}/lib"
            NO_DEFAULT_PATH
        )

        if(PG_INCLUDE_DIR AND PG_LIBRARY)
            set(PostgreSQL_INCLUDE_DIRS ${PG_INCLUDE_DIR} PARENT_SCOPE)
            set(PostgreSQL_LIBRARIES ${PG_LIBRARY} PARENT_SCOPE)
            set(PostgreSQL_FOUND TRUE PARENT_SCOPE)
            message(STATUS "libpq found: include=${PG_INCLUDE_DIR}, library=${PG_LIBRARY}")
        else()
            message(FATAL_ERROR "libpq-fe.h or libpq library not found in ${VCPKG_INSTALLED}")
        endif()

    else()
        # Linux: standard search via pkg-config and manual fallback
        find_package(PkgConfig)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(PG libpq)
            if(PG_FOUND)
                set(PostgreSQL_LIBRARIES ${PG_LIBRARIES} PARENT_SCOPE)
                set(PostgreSQL_INCLUDE_DIRS ${PG_INCLUDE_DIRS} PARENT_SCOPE)
                set(PostgreSQL_FOUND TRUE PARENT_SCOPE)
                return()
            endif()
        endif()
        find_path(PostgreSQL_INCLUDE_DIR NAMES libpq-fe.h
            HINTS $ENV{PG_ROOT}/include /usr/include/postgresql
            PATH_SUFFIXES include
        )
        find_library(PostgreSQL_LIBRARY NAMES pq libpq
            HINTS $ENV{PG_ROOT}/lib
            PATH_SUFFIXES lib
        )
        if(PostgreSQL_INCLUDE_DIR AND PostgreSQL_LIBRARY)
            set(PostgreSQL_FOUND TRUE PARENT_SCOPE)
            set(PostgreSQL_INCLUDE_DIRS ${PostgreSQL_INCLUDE_DIR} PARENT_SCOPE)
            set(PostgreSQL_LIBRARIES ${PostgreSQL_LIBRARY} PARENT_SCOPE)
            return()
        endif()
        message(FATAL_ERROR
            "libpq not found on Linux.\n"
            "Install: sudo apt install libpq-dev\n"
            "Or set PG_ROOT environment variable."
        )
    endif()
endfunction()