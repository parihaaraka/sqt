cmake_minimum_required(VERSION 3.10)
project(sqt)

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
