cpmaddpackage(
    NAME
    tracy
    GITHUB_REPOSITORY
    wolfpld/tracy
    GIT_TAG
    v0.13.1
    GIT_SHALLOW
    ON
    EXCLUDE_FROM_ALL
    ON
    SYSTEM
    ON
    OPTIONS
    "TRACY_STATIC ON"
    "TRACY_ENABLE ON")

if(NOT tracy_ADDED)
    return()
endif()

if(NOT BUILD_PROFILER)
    return()
endif()

# ── Tracy profiler GUI (host tool) ──────────────────────────────────
include(GNUInstallDirs)
include(ExternalProject)

set(TRACY_TOOLS_INSTALL_DIR
    "${CMAKE_INSTALL_LIBEXECDIR}/tracy"
    CACHE PATH "Installation directory for Tracy profiler tools")

set(tracy_profiler_prefix "${CMAKE_BINARY_DIR}/_deps/tracy-profiler")

externalproject_add(
    tracy_profiler
    SOURCE_DIR "${tracy_SOURCE_DIR}/profiler"
    PREFIX "${tracy_profiler_prefix}"
    BINARY_DIR "${tracy_profiler_prefix}/build"
    STAMP_DIR "${tracy_profiler_prefix}/stamp"
    TMP_DIR "${tracy_profiler_prefix}/tmp"
    INSTALL_DIR "${tracy_profiler_prefix}/install"
    DOWNLOAD_COMMAND ""
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release
               -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
               -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
               -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
               -DTRACY_NO_ISA_EXTENSIONS=ON
    BUILD_COMMAND
        "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release --parallel
    INSTALL_COMMAND
        "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release --prefix
        <INSTALL_DIR>
    BUILD_ALWAYS OFF
    USES_TERMINAL_BUILD ON
    USES_TERMINAL_CONFIGURE ON)

install(
    DIRECTORY "${tracy_profiler_prefix}/install/bin/"
    DESTINATION "${TRACY_TOOLS_INSTALL_DIR}"
    COMPONENT tools
    USE_SOURCE_PERMISSIONS OPTIONAL)

install(
    DIRECTORY "${tracy_profiler_prefix}/install/lib/"
    DESTINATION "${TRACY_TOOLS_INSTALL_DIR}"
    COMPONENT tools
    USE_SOURCE_PERMISSIONS OPTIONAL FILES_MATCHING
    PATTERN "*.so*"
    PATTERN "*.dylib*")
