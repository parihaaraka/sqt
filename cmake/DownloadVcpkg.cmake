function(download_vcpkg)
    set(VCPKG_DIR "${CMAKE_SOURCE_DIR}/vcpkg")
    if(EXISTS "${VCPKG_DIR}/vcpkg.exe")
        message(STATUS "vcpkg already exists at ${VCPKG_DIR}")
        set(VCPKG_EXE "${VCPKG_DIR}/vcpkg.exe" PARENT_SCOPE)
        return()
    endif()

    message(STATUS "vcpkg not found, downloading to ${VCPKG_DIR}...")
    file(MAKE_DIRECTORY ${VCPKG_DIR})

    set(VCPKG_RELEASE_URL "https://github.com/microsoft/vcpkg/archive/refs/heads/master.zip")
    set(VCPKG_ZIP "${VCPKG_DIR}/vcpkg-master.zip")

    file(DOWNLOAD ${VCPKG_RELEASE_URL} ${VCPKG_ZIP}
         SHOW_PROGRESS
         STATUS DOWNLOAD_STATUS
    )
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to download vcpkg: ${DOWNLOAD_STATUS}")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xzf ${VCPKG_ZIP}
        WORKING_DIRECTORY ${VCPKG_DIR}
        RESULT_VARIABLE UNZIP_RESULT
    )
    if(NOT UNZIP_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to unzip vcpkg")
    endif()

    # move vcpkg-master into VCPKG_DIR
    file(GLOB VCPKG_SRC_DIR "${VCPKG_DIR}/vcpkg-master/*")
    file(COPY ${VCPKG_SRC_DIR} DESTINATION ${VCPKG_DIR})
    file(REMOVE_RECURSE "${VCPKG_DIR}/vcpkg-master")
    file(REMOVE ${VCPKG_ZIP})

    # Bootstrapping
    message(STATUS "Bootstrapping vcpkg...")
    execute_process(
        COMMAND "${VCPKG_DIR}/bootstrap-vcpkg.bat"
        WORKING_DIRECTORY ${VCPKG_DIR}
        RESULT_VARIABLE BOOTSTRAP_RESULT
    )
    if(NOT BOOTSTRAP_RESULT EQUAL 0)
        message(FATAL_ERROR "vcpkg bootstrap failed")
    endif()

    set(VCPKG_EXE "${VCPKG_DIR}/vcpkg.exe" PARENT_SCOPE)
    message(STATUS "vcpkg installed to ${VCPKG_DIR}")
endfunction()
