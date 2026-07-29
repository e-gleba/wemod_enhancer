cpmaddpackage(
    NAME
    tinyobjloader
    GITHUB_REPOSITORY
    tinyobjloader/tinyobjloader
    GIT_TAG
    release # or a fixed version/tag, e.g. v1.0.6
    SYSTEM
    ON
    GIT_SHALLOW
    ON
    EXCLUDE_FROM_ALL
    ON
    OPTIONS
    "TINYOBJLOADER_BUILD_SHARED_LIBS OFF"
    "BUILD_SHARED_LIBS OFF"
    "CMAKE_BUILD_TYPE RelWithDebInfo"
    # Bloat Removal
    "TINYOBJLOADER_BUILD_TEST_LOADER OFF"
    "TINYOBJLOADER_BUILD_OBJ_STICHER OFF"
    "TINYOBJLOADER_WITH_PYTHON OFF"
    # Architecture
    "TINYOBJLOADER_USE_DOUBLE OFF" # Keep float for games/GPU
    # Installation
    "CMAKE_INSTALL_LIBDIR lib" # Dummy paths to silence install warnings if needed
    "CMAKE_INSTALL_INCLUDEDIR include")
