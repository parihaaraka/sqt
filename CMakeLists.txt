cmake_minimum_required(VERSION 3.16)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# Fills SQT_VERSION (numeric, for project() and the packages) and
# SQT_VERSION_STR (what the about box shows). Has to run before project().
include(SqtVersion)

# Same reason: the vcpkg triplet and the generator's make program are read by
# project() itself. No-op anywhere but on a windows MinGW build.
include(WindowsMinGWDefaults)

project(sqt VERSION ${SQT_VERSION})

set(OpenGL_GL_PREFERENCE "GLVND")

# Named once, used by the packages, the changelog and the about box alike.
set(SQT_MAINTAINER "Andrey Lukyanov <parihaaraka@gmail.com>")
set(SQT_HOMEPAGE "https://github.com/parihaaraka/sqt")


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

include(ProxySettings)

include(FindOrInstallQt)
include(FindOrInstallPostgreSQL)

find_or_install_qt6()
find_or_install_postgresql()
if(WIN32)
    set(ODBC_LIBRARIES odbc32 odbccp32)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ODBC REQUIRED odbc)
endif()

if(UNIX)
    find_package(OpenSSL REQUIRED)
endif()

file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "src/*.h" "src/*.hpp")
file(GLOB FORMS   "src/*.ui")
file(GLOB RESOURCES "src/*.qrc")
if(WIN32)
    file(GLOB RC_FILES "src/*.rc")
endif()

add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${FORMS}
    ${RESOURCES}
    ${RC_FILES}
)

# A gui program, so no console window behind it on windows. This is the portable
# spelling: the linker flag it used to be spelled with (-Wl,-subsystem,windows)
# only ever reached mingw's linker, and made the msvc one fail outright.
set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE ON)

# The version the about box shows. Comes from the tag through SqtVersion.cmake.
target_compile_definitions(${PROJECT_NAME} PRIVATE "SQT_VERSION_STR=\"${SQT_VERSION_STR}\"")

# The compiler finds a fair share of what otherwise has to be found by reading.
# Kept as warnings rather than errors: generated moc/uic code and the versions of
# Qt and libpq in the wild are not ours to keep quiet.
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

# ------------------- Deployment (Windows only) -------------------
if(WIN32)
    # windeployqt gathers the Qt dlls and plugins next to the exe. Looked for in
    # the bin directory of the Qt that was actually found - via qmake, which Qt
    # exports as a target - rather than in PATH: a PATH search may well find the
    # tool of a different Qt, or nothing at all when Qt was pointed at with
    # CMAKE_PREFIX_PATH alone.
    if(TARGET Qt6::qmake)
        get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    endif()
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}"
                    --no-compiler-runtime
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-translations
                    "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMENT "Deploying Qt libraries via windeployqt"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        )
    else()
        # Fatal, not a warning: without it the build still succeeds and the
        # package is still produced - only it holds not one Qt dll and cannot
        # start anywhere but on this machine.
        message(FATAL_ERROR
            "windeployqt not found (looked in '${_qt_bin_dir}' and PATH).\n"
            "It comes with Qt; point the build at Qt with CMAKE_PREFIX_PATH, "
            "or set WINDEPLOYQT_EXE to its full path.")
    endif()


    # PostgreSQL and dependency DLLs from vcpkg.
    #
    # The triplet actually in use, whenever it is known: guessing it was wrong
    # more often than not. A MinGW kit driven through vcpkg's toolchain ends up
    # on x64-mingw-*static* here, so the guess of x64-mingw-dynamic looked in a
    # directory that does not exist and warned about libpq.dll being missing -
    # while the build was linking libpq.a perfectly happily.
    if(DEFINED VCPKG_TARGET_TRIPLET)
        set(VCPKG_TRIPLET "${VCPKG_TARGET_TRIPLET}")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(VCPKG_TRIPLET "x64-mingw-dynamic")
    else()
        set(VCPKG_TRIPLET "x64-windows")
    endif()

    # Two roots, because the two ways of driving vcpkg put the tree in different
    # places: the manifest mode used by CI installs under the *binary* dir, the
    # classic install done by FindOrInstallPostgreSQL under the source one.
    set(VCPKG_BIN_DIR "")
    foreach(VCPKG_ROOT "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}")
        foreach(SUBDIR "" "/${VCPKG_TRIPLET}")
            set(TEST_DIR "${VCPKG_ROOT}/vcpkg_installed/${VCPKG_TRIPLET}${SUBDIR}/bin")
            if(EXISTS "${TEST_DIR}/libpq.dll")
                set(VCPKG_BIN_DIR "${TEST_DIR}")
                break()
            endif()
        endforeach()
        if(VCPKG_BIN_DIR)
            break()
        endif()
    endforeach()

    # What vcpkg's bin holds that this application does not link. libpq is
    # installed alongside its sibling client libraries, and copying the whole
    # directory took them along - so the archive carried an embedded-SQL
    # preprocessor runtime that nothing here has ever called.
    #
    # Stated as what to leave out rather than as a list of what to take: the
    # names of libpq's actual dependencies (z / zlib1, libcrypto-3-x64, ...)
    # vary between triplets and vcpkg versions, and an allow-list that goes
    # stale silently drops a dependency the program needs to start - a far
    # worse outcome than a spare 200 KB dll.
    set(VCPKG_DLL_EXCLUDE
        "libecpg"         # embedded SQL in C: preprocessor runtime
        "libecpg_compat"  # its compatibility layer
        "libpgtypes"      # ecpg's type conversion helpers
    )

    if(VCPKG_BIN_DIR)
        file(GLOB VCPKG_ALL_DLLS "${VCPKG_BIN_DIR}/*.dll")
        set(VCPKG_DLLS "")
        foreach(DLL ${VCPKG_ALL_DLLS})
            get_filename_component(DLL_NAME "${DLL}" NAME_WE)
            if(NOT DLL_NAME IN_LIST VCPKG_DLL_EXCLUDE)
                list(APPEND VCPKG_DLLS "${DLL}")
            endif()
        endforeach()
        if(VCPKG_DLLS)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VCPKG_DLLS} "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                COMMENT "Copying vcpkg DLLs from ${VCPKG_BIN_DIR}"
            )
        else()
            message(WARNING "No DLLs found in ${VCPKG_BIN_DIR}")
        endif()
    elseif(VCPKG_TRIPLET MATCHES "static")
        # Nothing to copy, and nothing wrong: a static triplet links libpq and
        # its dependencies into the exe. Said out loud all the same, because
        # "no dlls were copied" is worth telling apart from "the copy failed".
        message(STATUS
            "vcpkg triplet ${VCPKG_TRIPLET} is static; libpq is linked into the "
            "binary and no PostgreSQL DLLs need copying.")
    else()
        message(WARNING "Could not find bin directory with libpq.dll; PostgreSQL DLLs will not be copied.")
    endif()

    # The compiler's own runtime. windeployqt does not carry it, and msvc gets
    # it from InstallRequiredSystemLibraries further down instead.
    set(EXISTING_MINGW_DLLS "")
    if(MINGW)
        get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(MINGW_DLLS
            "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
            "${MINGW_BIN_DIR}/libstdc++-6.dll"
            "${MINGW_BIN_DIR}/libwinpthread-1.dll"
        )
        # Filter only existing files
        foreach(DLL ${MINGW_DLLS})
            if(EXISTS "${DLL}")
                list(APPEND EXISTING_MINGW_DLLS "${DLL}")
            endif()
        endforeach()
        if(EXISTING_MINGW_DLLS)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EXISTING_MINGW_DLLS} "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                COMMENT "Copying MinGW runtime DLLs"
            )
        else()
            message(WARNING "MinGW runtime DLLs not found; they may not be copied.")
        endif()
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    Qt6::Widgets
    Qt6::Qml
    ${PostgreSQL_LIBRARIES}
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} ${ODBC_LIBRARIES})
    if(MINGW)
        target_link_libraries(${PROJECT_NAME} ws2_32 advapi32)
    endif()
else()
    target_link_libraries(${PROJECT_NAME} ${ODBC_LIBRARIES} ${ODBC_LDFLAGS_OTHER})
    target_include_directories(${PROJECT_NAME} PRIVATE ${ODBC_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} ${OPENSSL_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${OPENSSL_INCLUDE_DIR})
endif()

# ------------------- decor & scripts next to the binary -------------------
# Symlinked where the platform supports it (so edits in the source tree apply
# without a rebuild), copied otherwise. See cmake/LinkOrCopyDir.cmake.
set(LINK_OR_COPY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LinkOrCopyDir.cmake")

foreach(ASSET_DIR decor scripts)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
                "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}/${ASSET_DIR}"
                "-DDST=$<TARGET_FILE_DIR:${PROJECT_NAME}>/${ASSET_DIR}"
                "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
                -P "${LINK_OR_COPY_DIR}"
        COMMENT "Linking (or copying) ${ASSET_DIR} next to ${PROJECT_NAME}"
        VERBATIM
    )
endforeach()

# ------------------- installation (unix) -------------------
# The portable layout (everything next to the binary) keeps working as it is;
# this only adds the system-wide one, where the binary and its files are told
# apart. ResourceLocator searches both, so the same build serves either.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    set(SQT_DATA_DIR "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}")

    # The data folder is reached by an absolute path and by one relative to the
    # binary. The latter is what keeps an installed tree working after it has
    # been moved, or installed into a prefix other than the configured one.
    # It is derived from the very directories the install uses, so that changing
    # CMAKE_INSTALL_DATADIR cannot make the two drift apart.
    file(RELATIVE_PATH SQT_DATA_RELDIR "${CMAKE_INSTALL_FULL_BINDIR}" "${SQT_DATA_DIR}")

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        "SQT_DATA_DIR=\"${SQT_DATA_DIR}\""
        "SQT_DATA_RELDIR=\"${SQT_DATA_RELDIR}\"")

    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY scripts decor DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})
    install(FILES sqt.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES sqt.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

    # Debian expects a copyright file and a changelog under /usr/share/doc/sqt,
    # and lintian reports either one missing as an error.
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME copyright)

    # The changelog is generated rather than kept in the tree: the git history
    # is the real one, and a file restating it would only go stale.
    find_program(GZIP_EXE gzip)
    if(GZIP_EXE)
        string(TIMESTAMP SQT_CHANGELOG_DATE "%a, %d %b %Y %H:%M:%S +0000" UTC)
        file(WRITE "${CMAKE_BINARY_DIR}/changelog"
"sqt (${SQT_VERSION}) unstable; urgency=medium

  * Released as ${SQT_VERSION_STR}. See the releases page and the commit
    history at ${SQT_HOMEPAGE} for what changed.

 -- ${SQT_MAINTAINER}  ${SQT_CHANGELOG_DATE}
")
        execute_process(COMMAND "${GZIP_EXE}" -9nf "${CMAKE_BINARY_DIR}/changelog")
        install(FILES "${CMAKE_BINARY_DIR}/changelog.gz"
                DESTINATION ${CMAKE_INSTALL_DOCDIR})
    endif()
endif()


# ------------------- installation (windows) -------------------
# The portable layout, everything in one folder: this is what gets zipped for a
# release. Built as an install tree rather than out of the build directory,
# where decor/scripts may well be symlinks into the source tree.
if(WIN32)
    install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION .)
    install(DIRECTORY scripts decor DESTINATION .)
    install(FILES LICENSE README.md DESTINATION .)

    if(WINDEPLOYQT_EXE)
        # Qt's dlls and plugins, gathered next to the installed exe. Runs at
        # install time (not at build time) so that the zip is complete even
        # when the build directory is not.
        install(CODE "
            execute_process(
                COMMAND \"${WINDEPLOYQT_EXE}\"
                        --no-compiler-runtime
                        --no-system-d3d-compiler
                        --no-opengl-sw
                        --no-translations
                        \"\${CMAKE_INSTALL_PREFIX}/${PROJECT_NAME}.exe\"
                RESULT_VARIABLE _windeployqt_result)
            if(NOT _windeployqt_result EQUAL 0)
                message(FATAL_ERROR \"windeployqt failed: \${_windeployqt_result}\")
            endif()")

        # What windeployqt leaves behind that this application cannot use.
        #
        # It deploys the entire QML tooling directory for anything linking Qml -
        # sqt links it for QJSEngine alone (see scripting.cpp) - and then
        # resolves the dependencies of the plugins it just copied: two of them,
        # qmldbg_inspector and qmldbg_quickprofiler, link Qt Quick, which brings
        # QtOpenGL and the Direct3D 12 shader compiler along. Some 30 MB in aid
        # of a QML debugger that can only be reached through the
        # -qmljsdebugger command line option, and a scene graph this
        # widget-based program never creates. None of it is what QJSEngine runs
        # on: that lives entirely inside Qt6Qml.dll, which stays.
        #
        # Removed after the fact rather than suppressed with a windeployqt
        # option: the relevant options differ between Qt versions, and an
        # unknown one makes the tool fail outright - so this list, unlike a
        # flag, cannot break the release when Qt is updated.
        set(SQT_DEPLOY_JUNK
            qmltooling              # QML debugger services; -qmljsdebugger only
            qml                     # QML module imports; empty, nothing imports any
            generic                 # qtuiotouchplugin: TUIO touch input over UDP
            Qt6Quick.dll            # required by the qmltooling plugins, by nothing else
            Qt6QmlModels.dll        # ditto
            Qt6QmlMeta.dll          # ditto
            Qt6QmlWorkerScript.dll  # ditto
            Qt6OpenGL.dll           # ditto; widgets paint through the raster engine
            dxcompiler.dll          # D3D12 shader compiler, reached only through Quick's RHI
            dxil.dll                # its shader signing companion
            imageformats/qgif.dll   # only png, svg and ico are ever loaded
            imageformats/qjpeg.dll  # ditto
            tls                     # QSslSocket backends; libpq brings its own TLS
            networkinformation      # connectivity reporting for QNetworkInformation
        )

        set(SQT_PRUNE_CODE "message(STATUS \"Pruning unused Qt deployment artifacts\")\n")
        foreach(JUNK IN LISTS SQT_DEPLOY_JUNK)
            string(APPEND SQT_PRUNE_CODE
                   "file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/${JUNK}\")\n")
        endforeach()
        # After the windeployqt call above: install code runs in the order it is
        # declared, so this always sees a fully populated tree.
        install(CODE "${SQT_PRUNE_CODE}")
    endif()

    if(VCPKG_DLLS)
        # The same filtered list the build directory gets, rather than the whole
        # of vcpkg's bin - see VCPKG_DLL_EXCLUDE above.
        install(FILES ${VCPKG_DLLS} DESTINATION .)
    endif()
    if(EXISTING_MINGW_DLLS)
        install(FILES ${EXISTING_MINGW_DLLS} DESTINATION .)
    endif()
    if(MSVC)
        # msvcp140.dll and friends, so the zip runs on a machine without the
        # redistributable installed.
        #
        # Deliberately *without* CMAKE_INSTALL_UCRT_LIBRARIES: that one adds an
        # app-local copy of the Universal CRT - ucrtbase.dll plus 44
        # api-ms-win-*.dll forwarders, i.e. 45 of the archive's ~90 files - and
        # it is only ever needed on Windows 7 or 8 that never received
        # KB2999226. Every Windows 10 and 11 has the UCRT as part of the system.
        # Should those be worth supporting again, turn this back on rather than
        # shipping the files by accident.
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
        # Collect the list without installing it, so the result can be filtered.
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP ON)
        include(InstallRequiredSystemLibraries)

        set(SQT_RUNTIME_LIBS "")
        foreach(RUNTIME_LIB ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
            # concrt140.dll is the Concurrency Runtime, which the MSVC standard
            # library stopped using after VS2015; nothing in this program or in
            # Qt loads it.
            if(NOT RUNTIME_LIB MATCHES "concrt140")
                list(APPEND SQT_RUNTIME_LIBS "${RUNTIME_LIB}")
            endif()
        endforeach()
        install(PROGRAMS ${SQT_RUNTIME_LIBS} DESTINATION .)
    endif()
endif()

# ------------------- packaging -------------------
# `cpack -G DEB` / `-G ZIP` in the build directory, or `--target package`.
# The deb is a plain package built out of the install tree above; there is no
# debian/ directory, which would only be worth writing for an actual upload to
# the distribution.
set(CPACK_PACKAGE_NAME "sqt")
set(CPACK_PACKAGE_VENDOR "parihaaraka")
set(CPACK_PACKAGE_VERSION "${SQT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Scriptable SQL client for PostgreSQL and ODBC data sources")
# Debian wants a body under the summary line, and lintian says so out loud.
set(CPACK_PACKAGE_DESCRIPTION
    "sqt is a query tool whose object tree, object DDL and preview panes are\n\
produced by plain SQL scripts, so the way it presents a database can be\n\
adjusted without touching the application. It talks to PostgreSQL natively\n\
and to anything else through ODBC.")

set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/parihaaraka/sqt")
set(CPACK_PACKAGE_CONTACT "Andrey Lukyanov <parihaaraka@gmail.com>")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "sqt")
set(CPACK_STRIP_FILES ON)

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    # One folder inside the archive, so unpacking never scatters dlls about.
    set(CPACK_PACKAGE_FILE_NAME "sqt-${SQT_VERSION}-windows-x64")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB")
    # Read off the built binary rather than written by hand: a dependency list
    # maintained by hand goes stale the first time a library is added.
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_SECTION "database")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    # sqt_<version>_<arch>.deb, the name debian policy asks for.
    set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
endif()

include(CPack)

# ------------------- tests -------------------
option(SQT_BUILD_TESTS "Build unit tests" ON)

if(SQT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

