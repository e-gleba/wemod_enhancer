# ─── version.dll proxy target ───────────────────────────────────────
add_library(version SHARED fuses.c library.c library.def)

set_target_properties(
    version
    PROPERTIES PREFIX ""
               OUTPUT_NAME "version"
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_features(version PRIVATE c_std_11)

# Public headers via FILE_SET (3.23+): BASE_DIRS is added to the
# target's include directories for the build tree and rewritten to
# CMAKE_INSTALL_INCLUDEDIR on install, so both in-tree patch code and
# installed consumers see <wemod_enhancer/fuses.h> — and the install
# prefix ships include/ next to the import library in lib/.
# Ref: Professional CMake §28.1 "Target Sources", CMake FILE_SET docs
target_sources(
    version
    PUBLIC FILE_SET
           HEADERS
           BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
           FILES
               "${CMAKE_CURRENT_SOURCE_DIR}/include/wemod_enhancer/fuses.h")

# MinGW-Clang needs -static to link static libgcc; MSVC-ABI Clang uses
# MSVC_RUNTIME_LIBRARY (above) for static CRT and does not understand -static
# the same way through lld-link.
if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_C_COMPILER_TARGET MATCHES "msvc")
    target_link_options(version PRIVATE -static)
endif()

# ─── Custom patches ─────────────────────────────────────────────────
# Drop-in user patches: every .c file under WEMOD_ENHANCER_PATCHES_DIR
# is compiled into version.dll. Exactly one patch translation unit must
# define `void wemod_run_custom_patches(void)` — DllMain calls it right
# after the ASAR fuse flip. See patches/readme.md.
# CONFIGURE_DEPENDS re-globs on every rebuild, so dropping in a new
# file needs no manual re-configure (the one sanctioned file(GLOB) use).
set(WEMOD_ENHANCER_PATCHES_DIR
    "${wemod_enhancer_SOURCE_DIR}/patches"
    CACHE PATH "Directory with custom patch sources compiled into version.dll")

file(GLOB wemod_enhancer_patch_sources CONFIGURE_DEPENDS
     "${WEMOD_ENHANCER_PATCHES_DIR}/*.c")

if(wemod_enhancer_patch_sources)
    target_sources(version PRIVATE ${wemod_enhancer_patch_sources})
    target_compile_definitions(version
                               PRIVATE WEMOD_ENHANCER_HAS_CUSTOM_PATCHES=1)
    message(
        STATUS
            "wemod_enhancer: custom patches => ${wemod_enhancer_patch_sources}"
    )
endif()

# ─── Install & package config ───────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(wemod_enhancer_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/wemod_enhancer")

install(
    TARGETS version
    EXPORT wemod_enhancer-targets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" # MSVC import .lib
    FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

# find_package(wemod_enhancer CONFIG) support: the install tree exports
# the imported target wemod_enhancer::version with its include dir and
# import library already wired up.
# Ref: Professional CMake §30.5 "Package Configuration Files"
configure_package_config_file(
    "${wemod_enhancer_SOURCE_DIR}/cmake/wemod_enhancer-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/wemod_enhancer-config.cmake"
    INSTALL_DESTINATION "${wemod_enhancer_cmake_dir}")

# No ARCH_INDEPENDENT on purpose: version.dll is a compiled binary, so
# the version file keeps the pointer-size check and rejects consumers
# whose architecture does not match the installed prefix.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/wemod_enhancer-config-version.cmake"
    VERSION "${wemod_enhancer_VERSION}"
    COMPATIBILITY SameMajorVersion)

install(
    EXPORT wemod_enhancer-targets
    NAMESPACE wemod_enhancer::
    DESTINATION "${wemod_enhancer_cmake_dir}")

install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/wemod_enhancer-config.cmake"
          "${CMAKE_CURRENT_BINARY_DIR}/wemod_enhancer-config-version.cmake"
    DESTINATION "${wemod_enhancer_cmake_dir}")
