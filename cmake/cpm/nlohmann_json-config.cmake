cpmaddpackage(
    NAME
    nlohmann_json
    GITHUB_REPOSITORY
    nlohmann/json
    VERSION
    3.12.0
    GIT_TAG
    v3.12.0
    GIT_SHALLOW
    TRUE
    GIT_PROGRESS
    TRUE
    EXCLUDE_FROM_ALL
    YES
    SYSTEM
    YES
    OPTIONS
    "JSON_BuildTests OFF"
    "JSON_Install OFF"
    "JSON_MultipleHeaders OFF")
