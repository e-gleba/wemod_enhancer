# Nested llvm-mingw build of version.dll for Linux-hosted configures
# (one CMake process = one toolchain). Windows configures never include
# this file - they add_subdirectory(version_dll) instead.
include_guard(GLOBAL)

include(ExternalProject)

find_program(version_dll_ninja NAMES ninja ninja-build REQUIRED)

if(CMAKE_CONFIGURATION_TYPES)
    set(version_dll_build_type RelWithDebInfo)
elseif(CMAKE_BUILD_TYPE)
    set(version_dll_build_type "${CMAKE_BUILD_TYPE}")
else()
    set(version_dll_build_type Release)
endif()

cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH version_dll_root)
set(version_dll_install "${CMAKE_BINARY_DIR}/_ep/version_dll-install")

ExternalProject_Add(
    wemod_version_dll
    SOURCE_DIR "${version_dll_root}/src/version_dll"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_ep/version_dll"
    INSTALL_DIR "${version_dll_install}"
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS "-DCMAKE_MAKE_PROGRAM=${version_dll_ninja}"
               "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_LIST_DIR}/toolchains/llvm_mingw.cmake"
               "-DCMAKE_SYSTEM_PROCESSOR=x86_64"
               "-DCMAKE_BUILD_TYPE=${version_dll_build_type}"
               "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
               "-DCMAKE_INSTALL_BINDIR=bin"
    UPDATE_COMMAND ""
    BUILD_BYPRODUCTS "${version_dll_install}/bin/version.dll"
    USES_TERMINAL_CONFIGURE TRUE
    USES_TERMINAL_BUILD TRUE
    USES_TERMINAL_INSTALL TRUE)

install(FILES "${version_dll_install}/bin/version.dll"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
