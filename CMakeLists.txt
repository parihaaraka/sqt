cmake_minimum_required(VERSION 3.10)
project(sqt)

set(OpenGL_GL_PREFERENCE "GLVND")
if(WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-subsystem,windows")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
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

# ------------------- Deployment (Windows only) -------------------
if(WIN32)
    # Qt deployment using windeployqt
    find_program(WINDEPLOYQT_EXE windeployqt)
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
        message(WARNING "windeployqt not found; Qt DLLs will not be deployed automatically.")
    endif()

    # PostgreSQL and dependency DLLs from vcpkg
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(VCPKG_TRIPLET "x64-mingw-dynamic")
    else()
        set(VCPKG_TRIPLET "x64-windows")
    endif()

    set(VCPKG_INSTALLED_BASE "${CMAKE_SOURCE_DIR}/vcpkg_installed/${VCPKG_TRIPLET}")
    set(VCPKG_BIN_DIR "")
    foreach(SUBDIR "" "${VCPKG_TRIPLET}")
        set(TEST_DIR "${VCPKG_INSTALLED_BASE}/${SUBDIR}/bin")
        if(EXISTS "${TEST_DIR}/libpq.dll")
            set(VCPKG_BIN_DIR "${TEST_DIR}")
            break()
        endif()
    endforeach()

    if(VCPKG_BIN_DIR)
        file(GLOB VCPKG_DLLS "${VCPKG_BIN_DIR}/*.dll")
        if(VCPKG_DLLS)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VCPKG_DLLS} "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
                COMMENT "Copying vcpkg DLLs from ${VCPKG_BIN_DIR}"
            )
        else()
            message(WARNING "No DLLs found in ${VCPKG_BIN_DIR}")
        endif()
    else()
        message(WARNING "Could not find bin directory with libpq.dll; PostgreSQL DLLs will not be copied.")
    endif()

    # Copy MinGW runtime DLLs (libgcc, libstdc++, libwinpthread)
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(MINGW_DLLS
        "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
        "${MINGW_BIN_DIR}/libstdc++-6.dll"
        "${MINGW_BIN_DIR}/libwinpthread-1.dll"
    )
    # Filter only existing files
    set(EXISTING_MINGW_DLLS "")
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

# -------------------------------------------------------------------
set(DECOR_SRC "${CMAKE_CURRENT_SOURCE_DIR}/decor")
set(SCRIPTS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
set(DECOR_DST "${CMAKE_CURRENT_BINARY_DIR}/decor")
set(SCRIPTS_DST "${CMAKE_CURRENT_BINARY_DIR}/scripts")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DECOR_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${DECOR_SRC} ${DECOR_DST}
    COMMENT "Copying decor to ${DECOR_DST}"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SCRIPTS_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SCRIPTS_SRC} ${SCRIPTS_DST}
    COMMENT "Copying scripts to ${SCRIPTS_DST}"
)
