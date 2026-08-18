cmake_minimum_required(VERSION 3.31)

# The `version` target name collides with CMake's built-in `version` pseudo-
# target in some generators; allow it.
if(POLICY CMP0037)
    cmake_policy(SET CMP0037 NEW)
endif()

project(
    wemod_enhancer
    VERSION 1.0.2
    DESCRIPTION "WeMod patcher: version.dll proxy + Python patcher"
    HOMEPAGE_URL "https://github.com/e-gleba/wemod_enhancer"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(WEMOD_ENHANCER_BUILD_GUI "Build the ImGui desktop app (opt-in)" OFF)

# ---------------------------------------------------------------------------
# CPM bootstrap
# ---------------------------------------------------------------------------
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(cpm CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    include(GNUInstallDirs)
endif()

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(src)
add_subdirectory(tools)

if(WEMOD_ENHANCER_BUILD_GUI)
    add_subdirectory(gui)
endif()

# ---------------------------------------------------------------------------
# Combined Linux package
# ---------------------------------------------------------------------------
# A single Linux archive carrying the native GUI (.elf), the Windows cross
# build (.exe + version.dll) and the Python CLI.
#
# CPack packages one build tree's install rules, so the merge happens at
# install time: both build trees are installed into a shared staging prefix,
# then packed. The cross tree is located via WEMOD_ENHANCER_MINGW_BUILD_DIR
# (defaults to the conventional preset binaryDir).
#
# Usage:
#   cmake --workflow --preset llvm-mingw-x86_64-full   # Windows cross
#   cmake --workflow --preset linux-native-full        # native .elf
#   cmake --build --preset linux-native-release --target package-combined
if(WEMOD_ENHANCER_BUILD_GUI
   AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND NOT CMAKE_CROSSCOMPILING
   AND PROJECT_IS_TOP_LEVEL)

    # Path to the already-built Windows cross build tree. Configurable so the
    # target is testable and not tied to the preset layout.
    set(WEMOD_ENHANCER_MINGW_BUILD_DIR
        "${CMAKE_BINARY_DIR}/../llvm-mingw-x86_64"
        CACHE PATH
        "Build tree of the llvm-mingw Windows cross build (for package-combined)")

    set(_combined_stage "${CMAKE_BINARY_DIR}/combined-stage")
    set(_combined_out
        "${CMAKE_BINARY_DIR}/wemod_enhancer-linux-combined-amd64.tar.xz")

    add_custom_target(
        package-combined
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_combined_stage}"
        # Native Linux tree (GUI .elf + python) -> stage
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
                --config Release --prefix "${_combined_stage}"
        # Windows cross tree (version.dll + gui .exe + python) -> same stage
        COMMAND "${CMAKE_COMMAND}" --install "${WEMOD_ENHANCER_MINGW_BUILD_DIR}"
                --config Release --prefix "${_combined_stage}"
        # Pack the merged staging prefix.
        COMMAND "${CMAKE_COMMAND}" -E tar cfJ "${_combined_out}"
                --format=gnutar -- "${_combined_stage}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        COMMENT "Assembling combined Linux archive: ${_combined_out}"
        VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Packaging
# ---------------------------------------------------------------------------
if(PROJECT_IS_TOP_LEVEL)
    set(CPACK_PACKAGE_NAME "wemod_enhancer")
    set(CPACK_PACKAGE_VENDOR "e-gleba")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/license.md")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/readme.md")
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}")
    set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-amd64")
    include(CPack)
endif()
