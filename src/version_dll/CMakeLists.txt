cmake_minimum_required(VERSION 3.31)

# Standalone so ExternalProject can configure this directory without the
# parent tree (no CPM, no GUI, no extra languages). add_subdirectory()
# from the parent is equally valid when the parent already targets
# Windows (MSVC, Clang GNU driver, or llvm-mingw).
# Ref: Professional CMake §27 "Superbuilds", §8.3
project(wemod_version_dll LANGUAGES C)

add_library(version SHARED fuses.c library.c library.def)

set_target_properties(
    version
    PROPERTIES PREFIX ""
               OUTPUT_NAME "version"
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_features(version PRIVATE c_std_11)

# llvm-mingw / Clang GNU driver: no libgcc_s / libwinpthread next to the
# DLL. MSVC frontend does not understand -static; the MT CRT above is
# the equivalent.
target_link_options(
    version
    PRIVATE
    "$<$<AND:$<C_COMPILER_ID:Clang>,$<NOT:$<C_COMPILER_FRONTEND_VARIANT:MSVC>>>:-static>"
)

include(GNUInstallDirs)
install(TARGETS version RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
