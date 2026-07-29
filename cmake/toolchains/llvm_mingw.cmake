# cmake/toolchains/llvm_mingw.cmake
#
# Cross-compilation toolchain: Linux host -> Windows target (llvm-mingw).
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/llvm_mingw.cmake \
#              -DCMAKE_SYSTEM_PROCESSOR=x86_64|i686|aarch64
#
# Tunables (all overridable via -D or cache):
#   LLVM_MINGW_VERSION       — release tag  (default: 20260421)
#   LLVM_MINGW_HOST_OS       — package OS suffix (default: ubuntu-22.04)
#   LLVM_MINGW_AUTO_DOWNLOAD — fetch if absent   (default: ON)

include_guard(GLOBAL)

# ── target system ─────────────────────────────────────────────────────────────
# Must be set before project() sees the toolchain file.
set(CMAKE_SYSTEM_NAME Windows)

if(NOT DEFINED CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
endif()

# ── tunables ──────────────────────────────────────────────────────────────────
set(LLVM_MINGW_VERSION
    "20260421"
    CACHE STRING "llvm-mingw release tag")
set(LLVM_MINGW_HOST_OS
    "ubuntu-22.04"
    CACHE STRING "llvm-mingw host OS package suffix")
option(LLVM_MINGW_AUTO_DOWNLOAD "Download llvm-mingw if absent" ON)

# ── host arch ─────────────────────────────────────────────────────────────────
# Separate from CMAKE_HOST_SYSTEM_PROCESSOR: that variable is not always set
# on older CMake; the MATCHES form is robust across Linux/macOS CI runners.
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(llvm_mingw_host_arch aarch64)
else()
    set(llvm_mingw_host_arch x86_64)
endif()

# ── target triple ─────────────────────────────────────────────────────────────
# llvm-mingw ships one sysroot directory per Windows ABI target.
# The triple must match exactly or the linker will pick up wrong CRT objects.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(llvm_mingw_triple aarch64-w64-mingw32)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    set(llvm_mingw_triple x86_64-w64-mingw32)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^i[3-6]86$")
    set(llvm_mingw_triple i686-w64-mingw32)
else()
    message(
        FATAL_ERROR
            "llvm_mingw: unsupported CMAKE_SYSTEM_PROCESSOR='${CMAKE_SYSTEM_PROCESSOR}'\n"
            "Supported: x86_64, i686, aarch64")
endif()

# ── paths ─────────────────────────────────────────────────────────────────────
# Anchor to the project root (two levels up from cmake/toolchains/).
# CMAKE_SOURCE_DIR is NOT used here: it breaks in try_compile() sub-projects
# and FetchContent calls, where CMAKE_SOURCE_DIR points at the sub-project.
cmake_path(
    GET
    CMAKE_CURRENT_LIST_DIR
    PARENT_PATH
    llvm_mingw_cmake_dir)
cmake_path(
    GET
    llvm_mingw_cmake_dir
    PARENT_PATH
    llvm_mingw_project_root)

set(llvm_mingw_install_dir "${llvm_mingw_project_root}/llvm_mingw")
set(llvm_mingw_pkg
    "llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-${LLVM_MINGW_HOST_OS}-${llvm_mingw_host_arch}"
)
set(llvm_mingw_url
    "https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/${llvm_mingw_pkg}.tar.xz"
)
set(llvm_mingw_archive "${llvm_mingw_project_root}/${llvm_mingw_pkg}.tar.xz")
set(llvm_mingw_sysroot "${llvm_mingw_install_dir}/${llvm_mingw_triple}")

# ── download / extract ────────────────────────────────────────────────────────
# Probe the target-specific clang binary so a partial install (e.g. x86_64
# present but i686 missing) is detected correctly per-triple.
if(NOT EXISTS "${llvm_mingw_install_dir}/bin/${llvm_mingw_triple}-clang")
    if(NOT LLVM_MINGW_AUTO_DOWNLOAD)
        message(
            FATAL_ERROR
                "llvm-mingw: toolchain not found at '${llvm_mingw_install_dir}'\n"
                "Set -DLLVM_MINGW_AUTO_DOWNLOAD=ON or extract '${llvm_mingw_pkg}' there manually."
        )
    endif()

    message(STATUS "llvm-mingw: fetching '${llvm_mingw_pkg}'")
    file(
        DOWNLOAD "${llvm_mingw_url}" "${llvm_mingw_archive}"
        SHOW_PROGRESS
        STATUS llvm_mingw_dl_status
        TLS_VERIFY ON)

    list(
        GET
        llvm_mingw_dl_status
        0
        llvm_mingw_dl_code)
    if(NOT
       llvm_mingw_dl_code
       EQUAL
       0)
        list(
            GET
            llvm_mingw_dl_status
            1
            llvm_mingw_dl_msg)
        file(REMOVE "${llvm_mingw_archive}")
        message(
            FATAL_ERROR "llvm-mingw: download failed => ${llvm_mingw_dl_msg}")
    endif()

    message(STATUS "llvm-mingw: extracting '${llvm_mingw_pkg}.tar.xz'")
    file(
        ARCHIVE_EXTRACT
        INPUT
        "${llvm_mingw_archive}"
        DESTINATION
        "${llvm_mingw_project_root}")
    file(REMOVE_RECURSE "${llvm_mingw_install_dir}")
    file(RENAME "${llvm_mingw_project_root}/${llvm_mingw_pkg}"
         "${llvm_mingw_install_dir}")
    file(REMOVE "${llvm_mingw_archive}")
    message(STATUS "llvm-mingw: installed => '${llvm_mingw_install_dir}'")
endif()

# ── compilers & tools ─────────────────────────────────────────────────────────
# CACHE FILEPATH "" FORCE is required: CMake's compiler detection writes these
# to CMakeCache.txt on first configure; FORCE ensures the toolchain always wins
# over a stale cache entry left by a previous preset run.
set(CMAKE_C_COMPILER
    "${llvm_mingw_install_dir}/bin/${llvm_mingw_triple}-clang"
    CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER
    "${llvm_mingw_install_dir}/bin/${llvm_mingw_triple}-clang++"
    CACHE FILEPATH "" FORCE)
set(CMAKE_RC_COMPILER
    "${llvm_mingw_install_dir}/bin/${llvm_mingw_triple}-windres"
    CACHE FILEPATH "" FORCE)
set(CMAKE_AR
    "${llvm_mingw_install_dir}/bin/llvm-ar"
    CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB
    "${llvm_mingw_install_dir}/bin/llvm-ranlib"
    CACHE FILEPATH "" FORCE)
set(CMAKE_LINKER
    "${llvm_mingw_install_dir}/bin/ld.lld"
    CACHE FILEPATH "" FORCE)

# ── sysroot ───────────────────────────────────────────────────────────────────
# CMAKE_SYSROOT must NOT go into the cache. If cached, it survives preset
# switches (e.g. x86_64 -> i686) and poisons subsequent configures even with
# FORCE, because the cache is read before the toolchain file re-runs.
#
# Belt-and-suspenders: also bake --sysroot into *_FLAGS_INIT. These variables
# are toolchain-file-scoped (never written to CMakeCache.txt), so they always
# reflect the current triple regardless of cache state.
set(CMAKE_SYSROOT "${llvm_mingw_sysroot}")

set(CMAKE_C_FLAGS_INIT "--sysroot=${llvm_mingw_sysroot}")
set(CMAKE_CXX_FLAGS_INIT "--sysroot=${llvm_mingw_sysroot}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld --sysroot=${llvm_mingw_sysroot}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "-fuse-ld=lld --sysroot=${llvm_mingw_sysroot}")

# ── find_* scoping ────────────────────────────────────────────────────────────
# Restrict all find_library/find_package/find_path calls to the target sysroot.
# PROGRAM stays at NEVER so host tools (ninja, python, etc.) remain reachable.
set(CMAKE_FIND_ROOT_PATH "${llvm_mingw_sysroot}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ── clang-tidy ────────────────────────────────────────────────────────────────
# llvm-mingw releases do not ship clang-tidy. The host's clang-tidy is
# version-mismatched against the bundled libc++ headers and produces false
# errors (e.g. __countr_zero not found in <algorithm>). Disable unconditionally
# for this toolchain; enable it only in native presets.
set(CMAKE_C_CLANG_TIDY
    ""
    CACHE STRING "" FORCE)
set(CMAKE_CXX_CLANG_TIDY
    ""
    CACHE STRING "" FORCE)
