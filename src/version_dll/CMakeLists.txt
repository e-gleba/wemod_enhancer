# Dual-use: add_subdirectory from a Windows parent, or ExternalProject
# child on Linux hosts.
cmake_minimum_required(VERSION 3.31)
project(wemod_version_dll LANGUAGES C)

if(PROJECT_IS_TOP_LEVEL)
    include(GNUInstallDirs)
endif()

# The target is NOT named "version": SDL3 links the Windows system lib
# "version" by bare name, and an in-build target with that name would be
# bound into SDL3's link interface, breaking its install(EXPORT).
add_library(wemod_version_dll SHARED)
target_sources(wemod_version_dll PRIVATE fuses.c library.c library.def)
target_compile_features(wemod_version_dll PRIVATE c_std_11)

set_target_properties(
    wemod_version_dll
    PROPERTIES PREFIX ""
               OUTPUT_NAME "version"
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Clang GNU driver: statically link the runtime, no libgcc_s/libwinpthread
# next to the DLL. MSVC frontend rejects -static; the MT CRT above is it.
target_link_options(
    wemod_version_dll
    PRIVATE
    "$<$<AND:$<C_COMPILER_ID:Clang>,$<NOT:$<C_COMPILER_FRONTEND_VARIANT:MSVC>>>:-static>"
)

# RUNTIME only: nothing links the proxy (WeMod loads it via DLL search
# order), so the import .lib is dead weight.
install(TARGETS wemod_version_dll RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
