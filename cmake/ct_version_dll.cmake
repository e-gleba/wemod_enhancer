# Nested Windows build of version.dll for a Linux-hosted GUI configure.
#
# One CMake process = one toolchain. A Linux GUI configure cannot
# add_subdirectory() a PE DLL. ExternalProject starts a second CMake
# with llvm-mingw. Recursion stops: the child SOURCE_DIR is
# src/version_dll, which has no GUI and no this module.
#
# Windows GUI configures do not include this file — they
# add_subdirectory(version_dll) and share MSVC or Clang (GNU driver).
#
# Ref: Professional CMake §27 "Superbuilds"
include_guard(GLOBAL)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "ct_version_dll: nested llvm-mingw build is Linux-host only")
endif()

include(ExternalProject)

find_program(WEMOD_NINJA ninja NAMES ninja-build REQUIRED)

# Child is always Ninja single-config. Map the parent's requested
# configuration onto CMAKE_BUILD_TYPE; GUI package presets use
# RelWithDebInfo.
if(CMAKE_CONFIGURATION_TYPES)
    set(version_dll_build_type RelWithDebInfo)
elseif(CMAKE_BUILD_TYPE)
    set(version_dll_build_type "${CMAKE_BUILD_TYPE}")
else()
    set(version_dll_build_type Release)
endif()

set(version_dll_toolchain
    "${PROJECT_SOURCE_DIR}/cmake/toolchains/llvm_mingw.cmake")
if(NOT EXISTS "${version_dll_toolchain}")
    message(FATAL_ERROR "ct_version_dll: missing ${version_dll_toolchain}")
endif()

set(version_dll_install "${CMAKE_BINARY_DIR}/_ep/version_dll-install")

# SOURCE_DIR is the isolated project (not the parent tree) so the child
# never sees WEMOD_ENHANCER_BUILD_GUI, CPM, or SDL.
# CMAKE_SYSTEM_PROCESSOR=x86_64: WeMod is a Windows x86-64 client.
# UPDATE_COMMAND "" : in-tree sources, no VCS step.
ExternalProject_Add(
    wemod_version_dll
    SOURCE_DIR "${PROJECT_SOURCE_DIR}/src/version_dll"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_ep/version_dll"
    INSTALL_DIR "${version_dll_install}"
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS
        "-DCMAKE_MAKE_PROGRAM=${WEMOD_NINJA}"
        "-DCMAKE_TOOLCHAIN_FILE=${version_dll_toolchain}"
        "-DCMAKE_SYSTEM_PROCESSOR=x86_64"
        "-DCMAKE_BUILD_TYPE=${version_dll_build_type}"
        "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
        "-DCMAKE_INSTALL_BINDIR=bin"
    UPDATE_COMMAND ""
    BUILD_BYPRODUCTS "${version_dll_install}/bin/version.dll"
    USES_TERMINAL_CONFIGURE TRUE
    USES_TERMINAL_BUILD TRUE
    USES_TERMINAL_INSTALL TRUE
    LOG_OUTPUT_ON_FAILURE TRUE)

# Install-time (CPack) copies the nested artifact next to the GUI.
# The file is a build product of wemod_version_dll, which is in ALL.
install(FILES "${version_dll_install}/bin/version.dll"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
