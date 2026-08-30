cmake_minimum_required(VERSION 3.31)

project(
    wemod_enhancer
    VERSION 1.0.5
    DESCRIPTION
        "Cross-platform Wand/WeMod Electron patcher and Windows ASAR fuse proxy"
    HOMEPAGE_URL "https://github.com/e-gleba/wemod_enhancer"
    LANGUAGES C CXX)

option(WEMOD_ENHANCER_BUILD_GUI "Build the ImGui desktop app (opt-in)" OFF)

# No C++20 modules in use — do not pay for the module dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Project modules, loaded by name via include(). CMAKE_CURRENT_LIST_DIR
# (not CMAKE_SOURCE_DIR) keeps this correct when the project is consumed
# via add_subdirectory()/FetchContent.
# Ref: Professional CMake §8.3 "Project-relative Variables"
list(
    APPEND
    CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_LIST_DIR}/cmake"
    "${CMAKE_CURRENT_LIST_DIR}/cmake/cpack"
    "${CMAKE_CURRENT_LIST_DIR}/cmake/code_quality")

include(ct_cpm) # dependency bootstrap: CPM + package configs
include(ct_warnings) # warnings interface target

if(PROJECT_IS_TOP_LEVEL)
    include(ct_clang_format)
    include(ct_clang_tidy)
    include(ct_clang_doc)
    include(ct_cpplint)
    include(ct_doxygen)
endif()

include(GNUInstallDirs)
add_subdirectory(src)

add_subdirectory(scripts)

# Testing and packaging only run when this project owns the build. A parent
# project pulling us in via add_subdirectory()/FetchContent controls both
# itself — we must not interfere.
# Ref: Professional CMake §8.3, §39 "FetchContent"
if(PROJECT_IS_TOP_LEVEL)
    include(ct_cpack)

    # include(CPack) reads all CPACK_* variables at include-time and
    # ignores later changes — keep it last.
    # Ref: Professional CMake §36.1 "Packaging Basics"
    include(CPack)
endif()
