cpmaddpackage(
    NAME
    stb
    GITHUB_REPOSITORY
    nothings/stb
    SYSTEM
    ON
    GIT_SHALLOW
    ON
    GIT_TAG
    master
    DOWNLOAD_ONLY
    YES)

if(stb_ADDED)
    add_library(
        stb
        INTERFACE
        EXCLUDE_FROM_ALL
        ON)

    # Create namespace alias for consistency
    add_library(stb::stb ALIAS stb EXCLUDE_FROM_ALL ON)

    target_include_directories(stb SYSTEM INTERFACE ${stb_SOURCE_DIR})
endif()
