# Windows version.dll proxy. Export forwarding is declared entirely by
# library.def; C++ only contains the proxy's process-attach behavior.
#
# The proxy forwards all 17 version API exports to version_orig.dll (a
# renamed copy of the real system version.dll). The forwarder target
# module name must differ from the proxy's OUTPUT_NAME so that both
# MSVC link.exe and lld-link create forwarder entries (not self-references)
# and the Windows loader resolves the real DLL at runtime.

# CMake 3.31+ defaults CMAKE_CXX_SCAN_FOR_MODULES to ON when a target
# requires cxx_std_20 or higher. This generates per-config .modmap
# response files and pollutes compile_commands.json with cross-config
# references that break clang-tidy in multi-config generators. No C++20
# modules are used here.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# ── Import library for forwarder target ─────────────────────────────────────
# MSVC link.exe requires the forwarder target's import library at link time
# to resolve forwarder symbols. lld-link and lld embed the forwarder string
# without it, but linking against the import library is harmless for them.
#
# version_orig_import.def lists the same exports as the real system
# version.dll. The generated import library satisfies MSVC's linker without
# requiring the actual DLL at build time (works for cross-compilation).

set(_import_def "${CMAKE_CURRENT_BINARY_DIR}/version_orig_import.def")
file(
    WRITE "${_import_def}"
    "LIBRARY version_orig\nEXPORTS\n"
    "    GetFileVersionInfoA\n"
    "    GetFileVersionInfoByHandle\n"
    "    GetFileVersionInfoExA\n"
    "    GetFileVersionInfoExW\n"
    "    GetFileVersionInfoSizeA\n"
    "    GetFileVersionInfoSizeExA\n"
    "    GetFileVersionInfoSizeExW\n"
    "    GetFileVersionInfoSizeW\n"
    "    GetFileVersionInfoW\n"
    "    VerFindFileA\n"
    "    VerFindFileW\n"
    "    VerInstallFileA\n"
    "    VerInstallFileW\n"
    "    VerLanguageNameA\n"
    "    VerLanguageNameW\n"
    "    VerQueryValueA\n"
    "    VerQueryValueW\n")

# Map CMAKE_SYSTEM_PROCESSOR to lib.exe /MACHINE: and dlltool -m values.
string(TOUPPER "${CMAKE_SYSTEM_PROCESSOR}" _proc_upper)
if(_proc_upper MATCHES "AMD64|X86_64")
    set(_lib_machine "X64")
    set(_dlltool_machine "i386:x86-64")
elseif(_proc_upper MATCHES "X86|I686")
    set(_lib_machine "X86")
    set(_dlltool_machine "i386")
elseif(_proc_upper MATCHES "ARM64|AARCH64")
    set(_lib_machine "ARM64")
    set(_dlltool_machine "aarch64")
else()
    message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# MSVC and clang-cl (MSVC ABI frontend) use lib.exe to generate import
# libraries from .def files. llvm-mingw uses llvm-dlltool.
set(_is_msvc_abi "$<OR:$<CXX_COMPILER_ID:MSVC>,$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>")

if(_proc_upper MATCHES "AMD64|X86_64|X86|I686|ARM64|AARCH64")
    # Generate import library. lib.exe and llvm-dlltool both accept .def
    # files and produce import libraries without requiring the actual DLL.
    set(_import_lib "${CMAKE_CURRENT_BINARY_DIR}/version_orig.lib")
    set(_import_lib_mingw "${CMAKE_CURRENT_BINARY_DIR}/libversion_orig.a")

    # MSVC/clang-cl path: use lib.exe (CMAKE_AR is lib.exe for MSVC)
    add_custom_command(
        OUTPUT "${_import_lib}"
        COMMAND "${CMAKE_AR}" /DEF:"${_import_def}" /MACHINE:${_lib_machine}
                /OUT:"${_import_lib}"
        DEPENDS "${_import_def}"
        COMMENT "generating version_orig import library (lib.exe)"
        VERBATIM)

    # MinGW path: use dlltool
    add_custom_command(
        OUTPUT "${_import_lib_mingw}"
        COMMAND "${CMAKE_DLLTOOL}" -d "${_import_def}" -m ${_dlltool_machine}
                -l "${_import_lib_mingw}"
        DEPENDS "${_import_def}"
        COMMENT "generating version_orig import library (dlltool)"
        VERBATIM)

    add_custom_target(
        version_orig_import_lib ALL
        DEPENDS "${_import_lib}" "${_import_lib_mingw}")
endif()

# ── Proxy target ────────────────────────────────────────────────────────────

add_library(
    version SHARED
    dll_main.cpp
    fuses.cpp
    fuses.hpp
    library.def)

add_dependencies(version version_orig_import_lib)

target_compile_features(version PRIVATE cxx_std_23)
target_compile_definitions(version PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

target_include_directories(
    version
    PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")

# Link against the generated import library so MSVC link.exe can resolve
# forwarder symbols. For lld-link/lld this is redundant but harmless.
target_link_libraries(
    version
    PRIVATE GSL
            $<IF:${_is_msvc_abi},${_import_lib},${_import_lib_mingw}>)

target_link_options(
    version
    PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:Clang>,$<NOT:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>>:-static>)

set_target_properties(
    version
    PROPERTIES PREFIX ""
               OUTPUT_NAME "version"
               CXX_EXTENSIONS OFF
               CXX_SCAN_FOR_MODULES OFF
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(GNUInstallDirs)
install(
    TARGETS version
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
