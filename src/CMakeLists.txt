# SPDX-License-Identifier: GPL-3.0-or-later

# version.dll proxy - WeMod Pro unlocker
#
# Build configurations:
#   - Windows MSVC:  cmake --preset windows-msvc-x86_64
#   - Windows MinGW: cmake --preset windows-llvm-mingw-x86_64
#   - Cross-compile: cmake --preset llvm-mingw-x86_64 (on Linux)

# Sources
set(VERSION_SOURCES
    dll_main.c
    hooks.c
    hooks.h
    ini_config.c
    ini_config.h
    logger.c
    logger.h
    proxy.c
    proxy.h
    version.def
)

# Create the DLL
add_library(version SHARED ${VERSION_SOURCES})
add_library(wemod::version ALIAS version)

# Properties
set_target_properties(version PROPERTIES
    OUTPUT_NAME "version"
    PREFIX ""
    C_STANDARD 23
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

# Definitions
target_compile_definitions(version PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    $<$<CONFIG:Debug>:DEBUG;_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# Compiler options
if(MSVC)
    target_compile_options(version PRIVATE /W4 /utf-8 /wd4100)
    target_link_options(version PRIVATE /INCREMENTAL:NO)
else()
    target_compile_options(version PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Link libraries
target_link_libraries(version PRIVATE
    kernel32
    user32
    advapi32
    shlwapi
    $<$<BOOL:${MINGW}>:-static>
    $<$<BOOL:${MINGW}>:-static-libgcc>
)

# Install
install(TARGETS version
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
