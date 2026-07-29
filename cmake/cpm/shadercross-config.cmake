cpmaddpackage(
    NAME
    SDL_shadercross
    GITHUB_REPOSITORY
    libsdl-org/SDL_shadercross
    GIT_TAG
    main
    SYSTEM
    ON
    GIT_SHALLOW
    ON
    EXCLUDE_FROM_ALL
    ON
    OPTIONS
    "SDLSHADERCROSS_VENDORED ON" # Build/Fetch dependencies internally (spirv-cross, etc)
    "SDLSHADERCROSS_SHARED OFF"
    "SDLSHADERCROSS_STATIC ON"
    "SDLSHADERCROSS_SPIRVCROSS_SHARED OFF" # Force static linkage for internal deps
    "SDLSHADERCROSS_CLI OFF" # Kill the CLI tool
    "SDLSHADERCROSS_CLI_STATIC OFF" # Redundant, but explicit
    "SDLSHADERCROSS_CLI_LEAKCHECK OFF"
    "SDLSHADERCROSS_INSTALL OFF"
    "SDLSHADERCROSS_INSTALL_MAN OFF"
    "SDLSHADERCROSS_INSTALL_RUNTIME OFF" # Don't download runtime DLLs/libs
    "SDLSHADERCROSS_TESTS OFF"
    # CRITICAL PERFORMANCE NOTE:
    # Keep "ON" if you compile HLSL source.
    # Set "OFF" if you only do SPIR-V bytecode translation (saves massive build time/size).
    "SDLSHADERCROSS_DXC ON")
