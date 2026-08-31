# Isolated Windows PE proxy. Dual-use:
#   add_subdirectory from a Windows parent (MSVC / Clang GNU driver / llvm-mingw)
#   ExternalProject child with llvm-mingw (Linux GUI host)
#
# cmake_minimum_required / project / GNUInstallDirs run only when this
# directory is CMAKE_SOURCE_DIR (the ExternalProject child). A parent
# that add_subdirectory's us already called all three — repeating
# cmake_minimum_required in a subdirectory resets policies.
# Ref: Professional CMake §8.3, §27, §36.1
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.31)
    project(wemod_version_dll LANGUAGES C)
    include(GNUInstallDirs)
endif()

add_library(version SHARED)
target_sources(version PRIVATE fuses.c library.c library.def)
target_compile_features(version PRIVATE c_std_11)

set_target_properties(
    version
    PROPERTIES PREFIX ""
               OUTPUT_NAME \"version\"
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# llvm-mingw / Clang GNU driver: no libgcc_s / libwinpthread next to the
# DLL. MSVC frontend does not understand -static; the MT CRT above is
# the equivalent.
target_link_options(
    version
    PRIVATE
    "$<$<AND:$<C_COMPILER_ID:Clang>,$<NOT:$<C_COMPILER_FRONTEND_VARIANT:MSVC>>>:-static>"
)

# CMAKE_INSTALL_BINDIR comes from the parent GNUInstallDirs, or from the
# child's include above, or from ExternalProject -DCMAKE_INSTALL_BINDIR=bin.
# Only the RUNTIME artifact (the DLL itself) is installed: the import
# .lib is a build-time artifact and nothing links version.dll - WeMod
# loads it through DLL search order (proxy), so shipping the .lib would
# be dead weight.
install(TARGETS version RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
