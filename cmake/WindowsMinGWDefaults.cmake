# Defaults for a windows MinGW build. Has to be included *before* project(),
# because that is where both of the things it sets are consumed: the toolchain
# file (and with it vcpkg) and the generator's make program.
#
# Two separate traps, both sprung by QtCreator's "Desktop Qt ... MinGW" kit:
#
# 1. QtCreator's package manager auto setup (-DQT_CREATOR_ENABLE_PACKAGE_MANAGER_SETUP=ON)
#    spots vcpkg.json in the source tree and quietly injects vcpkg's toolchain,
#    <build>/vcpkg-dependencies/toolchain.cmake -> vcpkg/scripts/buildsystems/vcpkg.cmake.
#    That switches vcpkg into manifest mode, and manifest mode installs the
#    dependencies for vcpkg's *default* triplet, which on windows is
#    x64-windows, i.e. MSVC - no matter that the kit compiles with MinGW.
#    Building libpq that way fails, and project() dies with
#      vcpkg install failed. See logs ... vcpkg-manifest-install.log
#    Naming the mingw triplet up front is all it takes. vcpkg ships
#    x64-mingw-dynamic among its community triplets, so nothing has to be
#    written for it.
#
# 2. The "MinGW Makefiles" generator looks for mingw32-make.exe in PATH and
#    nowhere else, hence
#      CMake was unable to find a build program corresponding to "MinGW Makefiles".
#      CMAKE_MAKE_PROGRAM is not set.
#    Qt ships that tool right next to the compiler the kit names
#    (Qt/Tools/mingw1310_64/bin), so look there as well.
#
# Everything below is a default only: a value given on the command line, in the
# kit or in the cache wins.

# WIN32 is not set yet (project() is what determines it), so go by the host.
if(NOT CMAKE_HOST_WIN32)
    return()
endif()

# Is this a MinGW build? CMAKE_CXX_COMPILER_ID is likewise not known yet, so
# judge by what the kit passed in: the generator, or the compiler's name/path.
set(_sqt_mingw FALSE)
if(CMAKE_GENERATOR MATCHES "MinGW|MSYS")
    set(_sqt_mingw TRUE)
elseif(CMAKE_CXX_COMPILER)
    get_filename_component(_sqt_cxx_name "${CMAKE_CXX_COMPILER}" NAME_WE)
    if(_sqt_cxx_name MATCHES "(^|-)(g\\+\\+|gcc|c\\+\\+)$" OR
       CMAKE_CXX_COMPILER MATCHES "[Mm][Ii][Nn][Gg][Ww]")
        set(_sqt_mingw TRUE)
    endif()
endif()

if(NOT _sqt_mingw)
    return()
endif()

# ---- 1. vcpkg triplets ----
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic" CACHE STRING "vcpkg target triplet")
    message(STATUS "MinGW build: vcpkg target triplet set to ${VCPKG_TARGET_TRIPLET}")
endif()
if(NOT DEFINED VCPKG_HOST_TRIPLET)
    set(VCPKG_HOST_TRIPLET "x64-mingw-dynamic" CACHE STRING "vcpkg host triplet")
endif()

# ---- 2. the compiler's own bin directory ----
if(CMAKE_CXX_COMPILER)
    get_filename_component(_sqt_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
endif()

if(_sqt_mingw_bin AND EXISTS "${_sqt_mingw_bin}")
    # mingw32-make for the "MinGW Makefiles" generator.
    if(NOT CMAKE_MAKE_PROGRAM AND CMAKE_GENERATOR MATCHES "MinGW|MSYS")
        find_program(CMAKE_MAKE_PROGRAM NAMES mingw32-make make
                     HINTS "${_sqt_mingw_bin}")
        if(CMAKE_MAKE_PROGRAM)
            message(STATUS "MinGW build: CMAKE_MAKE_PROGRAM set to ${CMAKE_MAKE_PROGRAM}")
        else()
            message(WARNING
                "mingw32-make not found next to the compiler "
                "('${_sqt_mingw_bin}') nor in PATH. Either put it in PATH, set "
                "CMAKE_MAKE_PROGRAM, or build with the Ninja generator "
                "(ninja.exe comes with Qt, under Qt/Tools/Ninja).")
        endif()
    endif()

    # vcpkg builds libpq with whatever compiler it finds in PATH (the mingw
    # triplets pass PATH through untracked on purpose), and the kit does not
    # necessarily put the toolchain there.
    file(TO_NATIVE_PATH "${_sqt_mingw_bin}" _sqt_mingw_bin_native)
    string(FIND "$ENV{PATH}" "${_sqt_mingw_bin_native}" _sqt_path_pos)
    if(_sqt_path_pos EQUAL -1)
        set(ENV{PATH} "${_sqt_mingw_bin_native};$ENV{PATH}")
        message(STATUS "MinGW build: prepended ${_sqt_mingw_bin_native} to PATH for vcpkg")
    endif()
endif()

unset(_sqt_mingw)
unset(_sqt_cxx_name)
unset(_sqt_path_pos)
