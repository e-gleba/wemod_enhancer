cpmaddpackage(
    NAME
    freetype2_upstream
    GITHUB_REPOSITORY
    freetype/freetype
    EXCLUDE_FROM_ALL
    ON
    GIT_TAG
    VER-2-14-3
    OPTIONS
    "FT_DISABLE_BZIP2 ON"
    "FT_DISABLE_PNG ON"
    "FT_DISABLE_HARFBUZZ ON"
    "FT_DISABLE_BROTLI ON"
    "FT_WITH_ZLIB OFF"
    EXCLUDE_FROM_ALL
    YES)
