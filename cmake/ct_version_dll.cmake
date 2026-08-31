# Nested Windows PE build of version.dll for a Linux-hosted GUI configure.
#
# One CMake process = one toolchain. A Linux GUI configure cannot
# add_subdirectory() a PE DLL. ExternalProject starts a second CMake
# with llvm-mingw. Recursion stops: SOURCE_DIR is src/version_dll.
#
# Windows GUI configures do not include this file; they
# add_subdirectory(version_dll) and share MSVC or Clang (GNU driver).
#
# CMAKE_INSTALL_PREFIX / CMAKE_INSTALL_BINDIR are passed into the child
# so it does not invent its own layout. The parent already called
# include(GNUInstallDirs); this module only copies the nested artifact
# into the parent's bindir at install time.
#
# CMAKE_CURRENT_LIST_DIR (this file lives in cmake/) not PROJECT_SOURCE_DIR:
# the latter is wrong if a super-project add_subdirectory's us.
# Ref: Professional CMake §8.3, §27
include_guard(GLOBAL)

include(ExternalProject)

find_program(version_dll_ninja NAMES ninja ninja-build REQUIRED)

# Child is Ninja single-config. GUI package presets use RelWithDebInfo.
if(CMAKE_CONFIGURATION_TYPES)
    set(version_dll_build_type RelWithDebInfo)
elseif(CMAKE_BUILD_TYPE)
    set(version_dll_build_type "${CMAKE_BUILD_TYPE}")
else()
    set(version_dll_build_type Release)
endif()

set(version_dll_toolchain
    "${CMAKE_CURRENT_LIST_DIR}/toolchains/llvm_mingw.cmake")
set(version_dll_source "${CMAKE_CURRENT_LIST_DIR}/../src/version_dll")
set(version_dll_install "${CMAKE_BINARY_DIR}/_ep/version_dll-install")

ExternalProject_Add(
    wemod_version_dll
    SOURCE_DIR "${version_dll_source}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/_ep/version_dll"
    INSTALL_DIR "${version_dll_install}"
    CMAKE_GENERATOR "Ninja"
    CMAKE_ARGS "-DCMAKE_MAKE_PROGRAM=${version_dll_ninja}"
               "-DCMAKE_TOOLCHAIN_FILE=${version_dll_toolchain}"
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
