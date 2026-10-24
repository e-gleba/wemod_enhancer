# Dual-use: add_subdirectory from a Windows parent, or ExternalProject
# child on Linux hosts.
cmake_minimum_required(VERSION 3.31)
project(wemod_version_dll LANGUAGES C)

if(PROJECT_IS_TOP_LEVEL)
    include(GNUInstallDirs)
endif()

# On non-Windows hosts the proxy is a Windows PE built by a nested
# llvm-mingw project: one CMake process has one toolchain, so the main
# build drives it through ExternalProject and links nothing against it.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(ExternalProject)

    find_program(version_dll_ninja NAMES ninja ninja-build REQUIRED)

    if(CMAKE_CONFIGURATION_TYPES)
        set(version_dll_build_type RelWithDebInfo)
    elseif(CMAKE_BUILD_TYPE)
        set(version_dll_build_type "${CMAKE_BUILD_TYPE}")
    else()
        set(version_dll_build_type Release)
    endif()

    set(version_dll_install "${CMAKE_BINARY_DIR}/_ep/version_dll-install")

    ExternalProject_Add(
        wemod_version_dll
        SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}"
        BINARY_DIR "${CMAKE_BINARY_DIR}/_ep/version_dll"
        INSTALL_DIR "${version_dll_install}"
        CMAKE_GENERATOR "Ninja"
        CMAKE_ARGS "-DCMAKE_MAKE_PROGRAM=${version_dll_ninja}"
                   "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_LIST_DIR}/../../cmake/toolchains/llvm_mingw.cmake"
                   "-DCMAKE_SYSTEM_PROCESSOR=x86_64"
                   "-DCMAKE_BUILD_TYPE=${version_dll_build_type}"
                   "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
                   "-DCMAKE_INSTALL_BINDIR=bin"
        UPDATE_COMMAND ""
        BUILD_BYPRODUCTS "${version_dll_install}/bin/version.dll"
        USES_TERMINAL_CONFIGURE TRUE
        USES_TERMINAL_BUILD TRUE
        USES_TERMINAL_INSTALL TRUE)

    # Dev builds: stage the proxy next to the GUI binary, mirroring the
    # install layout, so a build-tree run finds it without installing.
    # Copy to an explicit file path after make_directory: copy_if_different
    # to a bare directory treats a missing multi-config directory (the
    # proxy builds before the GUI target, which owns creating it) as the
    # output file itself. That stray file then breaks MSVC MSB3191.
    # Ref: https://cmake.org/cmake/help/latest/command/add_custom_command.html
    if(TARGET wemod_enhancer_gui)
        add_custom_command(
            TARGET wemod_version_dll
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:wemod_enhancer_gui>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${version_dll_install}/bin/version.dll"
                    "$<TARGET_FILE_DIR:wemod_enhancer_gui>/version.dll"
            VERBATIM)
    endif()

    install(FILES "${version_dll_install}/bin/version.dll"
            DESTINATION "${CMAKE_INSTALL_BINDIR}")
    return()
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

# Dev builds: stage the proxy next to the GUI binary, mirroring the install
# layout, so a build-tree run finds it via --version-dll without installing.
# The GUI target is created first (src/CMakeLists.txt); TARGET genexes
# resolve at generate time, so the forward reference is safe. This block
# only exists in the native-Windows process - on Linux hosts this project
# is the ExternalProject child itself, which has no GUI target. Copy to an
# explicit file path after make_directory: copy_if_different to a bare
# directory treats a missing multi-config directory (this target builds
# before the GUI target, which owns creating it) as the output file
# itself. That stray file then breaks MSVC MSB3191.
# Ref: https://cmake.org/cmake/help/latest/command/add_custom_command.html
if(TARGET wemod_enhancer_gui)
    add_custom_command(
        TARGET wemod_version_dll
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:wemod_enhancer_gui>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:wemod_version_dll>"
                "$<TARGET_FILE_DIR:wemod_enhancer_gui>/version.dll"
        VERBATIM)
endif()
