# nlohmann/json package config — find_package(nlohmann_json CONFIG) lands
# here via CMAKE_PREFIX_PATH (cmake/cpm). Exposes nlohmann_json::nlohmann_json.
include_guard(GLOBAL)
cpmaddpackage(
    NAME
    nlohmann_json
    GITHUB_REPOSITORY
    nlohmann/json
    VERSION
    3.12.0
    GIT_TAG
    v3.12.0
    EXCLUDE_FROM_ALL
    YES
    SYSTEM
    YES
    OPTIONS
    "JSON_BuildTests OFF")
