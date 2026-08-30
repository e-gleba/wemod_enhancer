cmake_minimum_required(VERSION 3.31)

project(
    wemod_enhancer
    VERSION 1.0.5
    DESCRIPTION
        "Cross-platform Wand/WeMod Electron patcher and Windows ASAR fuse proxy"
    HOMEPAGE_URL "https://github.com/e-gleba/wemod_enhancer"
    LANGUAGES C CXX)

# No C++ modules in use: skip the CMP0155 source scan on every target.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Opt-in Dear ImGui frontend for the Python patcher CLI.
# OFF: the plain version.dll build never touches SDL3/imgui.
option(WEMOD_ENHANCER_BUILD_GUI "Build the ImGui desktop app (opt-in)" OFF)

include(GNUInstallDirs)

# CMAKE_CURRENT_LIST_DIR, not CMAKE_SOURCE_DIR: keeps working when the
# project is consumed via add_subdirectory()/FetchContent.
set(wemod_cmake_dirs
    "${CMAKE_CURRENT_LIST_DIR}/cmake"
    "${CMAKE_CURRENT_LIST_DIR}/cmake/description"
    "${CMAKE_CURRENT_LIST_DIR}/cmake/code_quality")
list(APPEND CMAKE_PREFIX_PATH ${wemod_cmake_dirs})

# Cross toolchain files may set CMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY,
# which hides the local *-config.cmake packages from find_package().
if(CMAKE_CROSSCOMPILING)
    list(APPEND CMAKE_FIND_ROOT_PATH ${wemod_cmake_dirs})
endif()

find_package(cpm CONFIG REQUIRED)
find_package(warnings CONFIG REQUIRED)

if(PROJECT_IS_TOP_LEVEL)
    find_package(clang_format CONFIG REQUIRED)
    find_package(clang_tidy CONFIG REQUIRED)
    find_package(clang_doc CONFIG REQUIRED)
    find_package(ct_doxygen CONFIG REQUIRED)
endif()

# version.dll and the GUI are mutually exclusive:
#   - src/ includes <windows.h> unconditionally: Windows targets only
#     (native or cross);
#   - a GUI configure never builds the DLL - the GUI downloads it from
#     the GitHub releases at runtime.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT WEMOD_ENHANCER_BUILD_GUI)
    add_subdirectory(src)
endif()

# Install rules, packaging, and the end-user GUI belong to the top-level
# build only: a parent project keeps control of testing and packaging.
if(PROJECT_IS_TOP_LEVEL)
    find_package(package_description CONFIG REQUIRED)

    # The Python patcher CLI ships inside the version.dll package; the GUI
    # downloads it at runtime, so a GUI package must not carry it.
    if(NOT WEMOD_ENHANCER_BUILD_GUI)
        add_subdirectory(scripts)
    endif()

    if(WEMOD_ENHANCER_BUILD_GUI)
        add_subdirectory(gui)
        set(CPACK_PACKAGE_NAME "wemod_enhancer_gui")
    endif()

    # e.g. "Windows-x86_64" vs "Windows-aarch64": per-arch packages must
    # not collide. Must be set before include(CPack) - it is read once.
    set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")

    # Strip packaged binaries; the build tree keeps RelWithDebInfo symbols.
    set(CPACK_STRIP_FILES ON)

    # include(CPack) reads the CPACK_* variables at include-time and
    # ignores later changes - keep it last.
    include(CPack)
endif()
