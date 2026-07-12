cmake_minimum_required(VERSION 3.10)
project(sqt)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ------------------- search Qt6 -------------------
find_package(Qt6 QUIET COMPONENTS Widgets Qml)

if(NOT Qt6_FOUND)
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
            # search Qt6 again with the new path
            find_package(Qt6 REQUIRED COMPONENTS Widgets Qml)
        endif()
    else()
        message(FATAL_ERROR
            "Qt6 not found. Please set CMAKE_PREFIX_PATH to your Qt installation prefix,\n"
            "or set Qt6_DIR to the directory containing Qt6Config.cmake.\n"
            "Example: cmake .. -DCMAKE_PREFIX_PATH=/home/me/Qt/6.10.1/gcc_64"
        )
    endif()
else()
    message(STATUS "Qt6 found via standard search")
endif()

# ------------------- PostgreSQL (libpq) -------------------
find_package(PostgreSQL)
if(NOT PostgreSQL_FOUND)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PG libpq)
        if(PG_FOUND)
            set(PostgreSQL_LIBRARIES ${PG_LIBRARIES})
            set(PostgreSQL_INCLUDE_DIRS ${PG_INCLUDE_DIRS})
            set(PostgreSQL_FOUND TRUE)
        endif()
    endif()
endif()

if(NOT PostgreSQL_FOUND)
    find_path(PostgreSQL_INCLUDE_DIR NAMES libpq-fe.h HINTS $ENV{PG_ROOT}/include /usr/include/postgresql PATH_SUFFIXES include)
    find_library(PostgreSQL_LIBRARY NAMES pq libpq HINTS $ENV{PG_ROOT}/lib PATH_SUFFIXES lib)
    if(PostgreSQL_INCLUDE_DIR AND PostgreSQL_LIBRARY)
        set(PostgreSQL_FOUND TRUE)
        set(PostgreSQL_INCLUDE_DIRS ${PostgreSQL_INCLUDE_DIR})
        set(PostgreSQL_LIBRARIES ${PostgreSQL_LIBRARY})
    else()
        message(FATAL_ERROR "PostgreSQL (libpq) not found. Set PG_ROOT or install libpq-dev.")
    endif()
endif()

# ------------------- ODBC -------------------
if(WIN32)
    set(ODBC_LIBRARIES odbc32 odbccp32)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ODBC REQUIRED odbc)
endif()

# ------------------- OpenSSL (только Linux) -------------------
if(UNIX)
    find_package(OpenSSL REQUIRED)
endif()

# --------------------------------------------------------------
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

# ------------------- copying decor и scripts -------------------
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
