# cxxopts package config — find_package(cxxopts CONFIG) lands here via
# CMAKE_PREFIX_PATH (cmake/cpm). Exposes cxxopts::cxxopts.
include_guard(GLOBAL)
cpmaddpackage(
    NAME
    cxxopts
    GITHUB_REPOSITORY
    jarro2783/cxxopts
    VERSION
    3.3.1
    GIT_TAG
    v3.3.1
    EXCLUDE_FROM_ALL
    YES
    SYSTEM
    YES
    OPTIONS
    "CXXOPTS_BUILD_EXAMPLES OFF"
    "CXXOPTS_BUILD_TESTS OFF"
    "CXXOPTS_ENABLE_INSTALL ON")
