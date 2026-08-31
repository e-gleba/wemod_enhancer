# Dependency bootstrap: fetch CPM itself, then resolve the project's
# packages through the config files in cmake/cpm/ (each wraps CPMAddPackage).
# Loaded once from the root CMakeLists.txt via include(cpm).

include(FetchContent)

# NOTE: no URL_HASH on purpose. Renovate bumps cpm_version but cannot
# recompute a hash — a stale hash fails the configure step harder than
# no hash. Same trust model as the GIT_TAG-pinned deps below.
set(cpm_version "0.43.1")

fetchcontent_declare(
    get_cpm
    URL "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${cpm_version}/CPM.cmake"
    DOWNLOAD_NO_EXTRACT TRUE)

fetchcontent_makeavailable(get_cpm)

include("${get_cpm_SOURCE_DIR}/CPM.cmake")

# Enable local package reuse (vcpkg, system, etc.)
# Ref: https://github.com/cpm-cmake/CPM.cmake#find_package-integration
set(CPM_USE_LOCAL_PACKAGES OFF)

# Keep clang-tidy out of the dependency source cache. Guarded: the cache
# dir only exists when CPM_SOURCE_CACHE is set (CI always sets it).
if(CPM_SOURCE_CACHE)
    file(WRITE "${CPM_SOURCE_CACHE}/.clang-tidy" "Checks: '-*'\n")
endif()

set(cpm_deps_dir "${CMAKE_CURRENT_LIST_DIR}/cpm")

list(APPEND CMAKE_PREFIX_PATH "${cpm_deps_dir}")
if(CMAKE_CROSSCOMPILING)
    # Toolchains may scope find_package() to CMAKE_FIND_ROOT_PATH
    # (CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY) — keep our configs reachable.
    list(APPEND CMAKE_FIND_ROOT_PATH "${cpm_deps_dir}")
endif()

# Every configure builds the full self-contained package (GUI +
# version.dll + patcher script) - the desktop dependencies are
# unconditional.
find_package(gsl CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)
find_package(sdl3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
