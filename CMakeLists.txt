cmake_minimum_required(VERSION 3.31)

project(
    cxx_project
    VERSION 1.0.0
    DESCRIPTION "My awesome C/C++ project!"
    HOMEPAGE_URL "https://github.com/e-gleba/cxx-skeleton"
    LANGUAGES C CXX)

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
    find_package(cpplint CONFIG REQUIRED)
    find_package(ct_doxygen CONFIG REQUIRED)
endif()

add_subdirectory(src)

# CTest, CPack, and install rules for top-level metadata files
# should only run when this project owns the build. If another
# project pulls us in via add_subdirectory() or FetchContent,
# it controls testing and packaging — we should not interfere.
# Ref: Professional CMake §8.3, §39 "FetchContent"
if(PROJECT_IS_TOP_LEVEL)
    # include(CTest) creates the BUILD_TESTING cache option
    # (default ON) and sets up CDash integration.
    include(CTest)

    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()

    include(GNUInstallDirs)
    find_package(package_description CONFIG REQUIRED)

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
