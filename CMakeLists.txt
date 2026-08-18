cmake_minimum_required(VERSION 3.31)

project(
    wemod_enhancer
    VERSION 1.0.0
    DESCRIPTION
        "Cross-platform patcher for WeMod: Pro unlock, DevTools, no auto-updates"
    HOMEPAGE_URL "https://github.com/e-gleba/wemod_enhancer"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Statically link the C++ runtime into every target so the produced
# binaries have no libstdc++/libc++ runtime dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(cmake/cpm-bootstrap.cmake)
include(cmake/cpm-config.cmake)
include(cmake/code_quality/clang_tidy-config.cmake)
include(cmake/code_quality/cpplint-config.cmake)
include(cmake/code_quality/warnings-config.cmake)

option(WEMOD_ENHANCER_BUILD_GUI "Build the ImGui GUI frontend (SDL3)" OFF)

if(WEMOD_ENHANCER_BUILD_GUI)
    add_subdirectory(gui)
endif()

if(WIN32)
    add_subdirectory(src)
endif()

if(NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(tests)
endif()

# The patcher CLI is a stdlib-only Python script — install it next to
# the executables so the GUI finds it at runtime.
install(FILES tools/wemod_enhancer.py DESTINATION bin)
