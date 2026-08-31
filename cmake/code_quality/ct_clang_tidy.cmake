# --- clang-tidy: static analysis ---------------------------------------------
# Two integration modes:
#   1. Co-compilation  - CMAKE_<LANG>_CLANG_TIDY (Makefiles/Ninja only)
#   2. Standalone      - run-clang-tidy wrapper target (Makefiles/Ninja only,
#                        needs compile_commands.json)
#
# Both use compile_commands.json so clang-tidy sees the *real*
# compiler flags, not a hardcoded -std=c++20.
# Settings live in .clang-tidy (YAML), not on the command line.

# Every module here creates named targets or cache entries: a double
# include would error on duplicates.  Guard once, globally.
include_guard(GLOBAL)

find_program(
    clang_tidy_exe
    NAMES clang-tidy
    DOC "clang-tidy static analyzer" OPTIONAL)

if(NOT clang_tidy_exe)
    message(NOTICE [[clang-tidy not found - static analysis disabled
  fedora:  sudo dnf install clang-tools-extra
  ubuntu:  sudo apt install clang-tidy
  alt:     sudo apt-get install clang-tools
  macos:   brew install llvm
  windows: choco install llvm]])
    return()
endif()

# --- compile_commands.json ----------------------------------------------------
# Negligible cost, enables all clang-based tools.
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# --- co-compilation (per-file, during build) ----------------------------------
# CMake >= 3.25: when the command line contains -p, the generator runs
# clang-tidy without appending `-- <compile flags>` - the tool resolves
# the real flags from the compilation database itself.
# Ref: cmake.org <LANG>_CLANG_TIDY, Professional CMake 32.1.1
# The project enables both C and CXX - lint both.
foreach(lang IN ITEMS C CXX)
    set(CMAKE_${lang}_CLANG_TIDY
        "${clang_tidy_exe}" -p "${CMAKE_BINARY_DIR}"
        CACHE STRING "clang-tidy co-compilation command")
endforeach()

# --- copy .clang-tidy into the build tree -------------------------------------
# Generated sources live in the build dir.  Without a
# .clang-tidy there, they get no settings (or wrong defaults)
# when the build dir is outside the source tree.
# configure_file (not file(COPY_FILE)) on purpose: COPYONLY registers
# .clang-tidy as a configure dependency, so editing the config re-runs
# CMake and refreshes the copy automatically.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
    configure_file(.clang-tidy .clang-tidy COPYONLY)
endif()

# --- standalone target (whole-project, parallel) ------------------------------
# run-clang-tidy uses the compilation database and runs
# clang-tidy in parallel across all TUs - far faster than
# a serial custom target.
# Only Makefile/Ninja generators emit compile_commands.json; on any
# other generator the target would configure but always fail.
if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    message(NOTICE
            "clang-tidy standalone target disabled - generator"
            " '${CMAKE_GENERATOR}' does not emit compile_commands.json")
    return()
endif()

find_program(
    run_clang_tidy_exe
    NAMES run-clang-tidy run-clang-tidy.py
    DOC "run-clang-tidy parallel wrapper")

if(run_clang_tidy_exe)
    add_custom_target(
        ${PROJECT_NAME}_clang_tidy
        COMMAND
            "${run_clang_tidy_exe}" -clang-tidy-binary "${clang_tidy_exe}"
            -p "${CMAKE_BINARY_DIR}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        VERBATIM
        COMMENT "running clang-tidy (parallel) on ${PROJECT_NAME}"
        USES_TERMINAL)
else()
    # Bracket args are verbatim (no ${} expansion) - keep the message
    # self-contained instead of mixing quoted and bracket parts.
    message(NOTICE [[run-clang-tidy not found - standalone target disabled
co-compilation via CMAKE_<LANG>_CLANG_TIDY still active]])
endif()
