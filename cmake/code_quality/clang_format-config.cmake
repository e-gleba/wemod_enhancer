# ─── clang-format ──────────────────────────────────────────────────
# Host-only text reformatter. No sysroot interaction, no
# compilation database — works identically when cross-compiling.

find_program(
    clang_format_exe
    NAMES clang-format
    DOC "clang-format source reformatter" OPTIONAL)

if(NOT clang_format_exe)
    message(
        NOTICE
        "clang-format not found -- format targets disabled\n"
        "  fedora:  sudo dnf install clang-tools-extra\n"
        "  ubuntu:  sudo apt install clang-format\n"
        "  macos:   brew install llvm\n"
        "  windows: choco install llvm")
    return()
endif()

# ── Verify .clang-format exists ────────────────────────────────
if(NOT EXISTS "${PROJECT_SOURCE_DIR}/.clang-format")
    message(NOTICE "no .clang-format at project root -- "
            "clang-format will use LLVM defaults")
endif()

# ── Collect sources ────────────────────────────────────────────
# GLOB_RECURSE is acceptable for developer tooling targets —
# missing a new file until reconfigure is harmless for formatting.
# CONFIGURE_DEPENDS re-globs on every build (Ninja/Makefiles).
set(format_scan_dirs "${PROJECT_SOURCE_DIR}/src"
                     "${PROJECT_SOURCE_DIR}/include")

set(format_sources "")
foreach(dir IN LISTS format_scan_dirs)
    if(IS_DIRECTORY "${dir}")
        file(
            GLOB_RECURSE
            dir_sources
            CONFIGURE_DEPENDS
            "${dir}/*.h"
            "${dir}/*.hpp"
            "${dir}/*.hxx"
            "${dir}/*.c"
            "${dir}/*.cpp"
            "${dir}/*.cxx"
            "${dir}/*.cc")
        list(APPEND format_sources ${dir_sources})
    endif()
endforeach()

if(NOT format_sources)
    message(NOTICE "clang-format: no sources found under "
            "${format_scan_dirs} -- adjust format_scan_dirs")
    return()
endif()

list(LENGTH format_sources format_count)

# ── Write file list for ARG_MAX safety ─────────────────────────
# Windows cmd.exe limit is ~32 768 chars.  At ~80 chars/path
# that's ~400 files before truncation — silently.
# Writing a newline-separated file list and feeding it through
# a cmake -P runner avoids the issue on every platform.
set(format_file_list "${CMAKE_CURRENT_BINARY_DIR}/clang-format-files.txt")
list(
    JOIN
    format_sources
    "\n"
    format_sources_joined)
file(WRITE "${format_file_list}" "${format_sources_joined}\n")

set(format_runner "${CMAKE_CURRENT_BINARY_DIR}/run-clang-format.cmake")
file(
    WRITE "${format_runner}"
    [=[
# Invoked by: cmake -D MODE=... -D CLANG_FORMAT=... -D FILE_LIST=... -P
cmake_minimum_required(VERSION 3.21)
file(STRINGS "${FILE_LIST}" sources)
if(MODE STREQUAL "check")
    set(args --dry-run --Werror)
else()
    set(args -i)
endif()

# Batch into chunks ≤ 30 000 chars to stay under ARG_MAX.
set(batch "")
set(batch_len 0)
foreach(src IN LISTS sources)
    string(LENGTH "${src}" src_len)
    math(EXPR next_len "${batch_len} + ${src_len} + 1")
    if(next_len GREATER 30000 AND batch)
        execute_process(
            COMMAND "${CLANG_FORMAT}" ${args} ${batch}
            COMMAND_ERROR_IS_FATAL ANY)
        set(batch "")
        set(batch_len 0)
    endif()
    list(APPEND batch "${src}")
    math(EXPR batch_len "${batch_len} + ${src_len} + 1")
endforeach()
if(batch)
    execute_process(
        COMMAND "${CLANG_FORMAT}" ${args} ${batch}
        COMMAND_ERROR_IS_FATAL ANY)
endif()
]=])

# ── Format target (modifies files in-place) ────────────────────
add_custom_target(
    ${PROJECT_NAME}-format
    COMMAND
        "${CMAKE_COMMAND}" -D "MODE=format" -D
        "CLANG_FORMAT=${clang_format_exe}" -D "FILE_LIST=${format_file_list}" -P
        "${format_runner}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VERBATIM
    COMMENT "clang-format: formatting ${format_count} files"
    USES_TERMINAL
    SOURCES ${format_sources})

# ── Check target (CI gate — fails on diff) ─────────────────────
# --dry-run + --Werror: clang-format ≥ 10.
add_custom_target(
    ${PROJECT_NAME}-format-check
    COMMAND
        "${CMAKE_COMMAND}" -D "MODE=check" -D "CLANG_FORMAT=${clang_format_exe}"
        -D "FILE_LIST=${format_file_list}" -P "${format_runner}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VERBATIM
    COMMENT "clang-format: checking ${format_count} files"
    USES_TERMINAL
    SOURCES ${format_sources})
