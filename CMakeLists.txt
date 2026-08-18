cmake_minimum_required(VERSION 3.31)

project(
    wemod_enhancer
    VERSION 1.0.2
    DESCRIPTION
        "Cross-platform Wand/WeMod Electron patcher and Windows ASAR fuse proxy"
    HOMEPAGE_URL "https://github.com/e-gleba/wemod_enhancer"
    LANGUAGES C CXX)

set(PROJECT_VENDOR "e-gleba")
set(PROJECT_CONTACT "evgeniy.gleba@yandex.ru")
set(PROJECT_LICENSE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/license.md")
set(PROJECT_README_FILE "${CMAKE_CURRENT_SOURCE_DIR}/readme.md")

# Opt-in Dear ImGui desktop frontend for the Python patcher CLI.
# OFF by default: the plain version.dll build never touches SDL3/imgui.
option(WEMOD_ENHANCER_BUILD_GUI "Build the ImGui desktop app (opt-in)" OFF)

# CMAKE_CURRENT_LIST_DIR is used instead of CMAKE_SOURCE_DIR so
# that this file works correctly if the project is ever consumed
# as a subdirectory of a larger build.
# Ref: Professional CMake §8.3 "Project-relative Variables"
list(
    APPEND
    CMAKE_PREFIX_PATH
    "${CMAKE_CURRENT_LIST_DIR}/cmake"
    "${CMAKE_CURRENT_LIST_DIR}/cmake/description"
    "${CMAKE_CURRENT_LIST_DIR}/cmake/code_quality")

# When cross-compiling, CMAKE_FIND_ROOT_PATH_MODE_PACKAGE is
# typically set to ONLY in the toolchain file. That restricts
# find_package() to searching only under CMAKE_FIND_ROOT_PATH
# and CMAKE_SYSROOT — our local cmake/ dirs wouldn't be found.
# Adding them to CMAKE_FIND_ROOT_PATH fixes this
if(CMAKE_CROSSCOMPILING)
    list(
        APPEND
        CMAKE_FIND_ROOT_PATH
        "${CMAKE_CURRENT_LIST_DIR}/cmake"
        "${CMAKE_CURRENT_LIST_DIR}/cmake/description"
        "${CMAKE_CURRENT_LIST_DIR}/cmake/code_quality")
endif()

find_package(cpm CONFIG REQUIRED)

find_package(warnings CONFIG REQUIRED)

if(PROJECT_IS_TOP_LEVEL)
    find_package(clang_format CONFIG REQUIRED)
    find_package(clang_tidy CONFIG REQUIRED)
    find_package(clang_doc CONFIG REQUIRED)
    find_package(ct_doxygen CONFIG REQUIRED)
endif()

# version.dll is Win32-only (src/ includes <windows.h> unconditionally):
# build it only when targeting Windows, native or cross. A native Linux
# configure (the gui-linux-* presets) has no Windows SDK to compile it.
if(WIN32)
    add_subdirectory(src)
endif()

# CTest, CPack, and install rules for top-level metadata files
# should only run when this project owns the build. If another
# project pulls us in via add_subdirectory() or FetchContent,
# it controls testing and packaging — we should not interfere.
# Ref: Professional CMake §8.3, §39 "FetchContent"
if(PROJECT_IS_TOP_LEVEL)
    include(GNUInstallDirs)
    find_package(package_description CONFIG REQUIRED)

    # Install the Python patcher CLI next to version.dll so the
    # install tree is self-contained: users need only Python (no CMake)
    # to run `python wemod_enhancer.py patch --install-dir ...`.
    add_subdirectory(scripts)

    # The GUI is an end-user artifact: build/install it only for
    # top-level builds, next to version.dll + wemod_enhancer.py.
    if(WEMOD_ENHANCER_BUILD_GUI)
        add_subdirectory(gui)
    endif()

    # Append processor to system name so packages for
    # different architectures don't collide (e.g.
    # "Windows-x86_64" vs "Windows-aarch64").
    string(APPEND CPACK_SYSTEM_NAME "-${CMAKE_SYSTEM_PROCESSOR}")

    # include(CPack) must come AFTER all CPACK_* variables
    # are set — it reads them at include-time and ignores
    # any changes made afterward.
    # Ref: Professional CMake §36.1 "Packaging Basics"
    include(CPack)
endif()
