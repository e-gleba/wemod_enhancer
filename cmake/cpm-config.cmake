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
set(CPM_USE_LOCAL_PACKAGES ON)

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

find_package(doctest CONFIG REQUIRED)
find_package(gsl CONFIG REQUIRED)

if(WEMOD_ENHANCER_BUILD_GUI)
    # imgui-config.cmake creates the imgui::sdl3_renderer backend target
    # only when SDL3::SDL3 already exists, so resolve SDL3 first.
    find_package(sdl3 CONFIG REQUIRED)
    # FreeType before imgui: imgui-config.cmake compiles
    # misc/freetype/imgui_freetype.cpp only when a freetype target
    # already exists. Resolved via CPM (cmake/cpm/freetype-config.cmake),
    # so the hinted rasterizer builds from the same pinned source on
    # every platform - no system package needed on CI.
    find_package(freetype CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)
endif()
