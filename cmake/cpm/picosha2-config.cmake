# PicoSHA2 package config — find_package(picosha2 CONFIG) lands here via
# CMAKE_PREFIX_PATH (cmake/cpm). Single-header SHA-256, MIT licensed
# (Copyright (C) 2017 okdshin). Pinned to an exact upstream commit because
# the project is unmaintained; re-pin deliberately, never floating.
# Origin: https://github.com/okdshin/PicoSHA2
include_guard(GLOBAL)
cpmaddpackage(
    NAME
    picosha2
    GITHUB_REPOSITORY
    okdshin/PicoSHA2
    GIT_TAG
    161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29
    EXCLUDE_FROM_ALL
    YES
    DOWNLOAD_ONLY
    YES)
if(DEFINED picosha2_SOURCE_DIR)
    add_library(picosha2 INTERFACE)
    add_library(picosha2::picosha2 ALIAS picosha2)
    target_include_directories(
        picosha2 SYSTEM INTERFACE $<BUILD_INTERFACE:${picosha2_SOURCE_DIR}>)
endif()
