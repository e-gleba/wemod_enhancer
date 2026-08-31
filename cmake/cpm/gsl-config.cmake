cpmaddpackage(
    NAME
    GSL
    GITHUB_REPOSITORY
    microsoft/GSL
    VERSION
    5.0.0
    GIT_SHALLOW
    TRUE
    GIT_PROGRESS
    TRUE
    EXCLUDE_FROM_ALL
    YES
    SYSTEM
    YES
    OPTIONS
    "GSL_TEST OFF"
    "GSL_INSTALL OFF")
