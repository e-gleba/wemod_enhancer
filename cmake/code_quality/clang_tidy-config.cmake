# ─── clang-tidy: static analysis ──────────────────────────────────
# Two integration modes:
#   1. Co-compilation  — CMAKE_<LANG>_CLANG_TIDY (Makefiles/Ninja only)
#   2. Standalone      — run-clang-tidy wrapper target (any generator)
#
# Both use compile_commands.json so clang-tidy sees the *real*
# compiler flags, not a hardcoded -std=c++20.
# Settings live in .clang-tidy (YAML), not on the command line.

find_program(
    clang_tidy_exe
    NAMES clang-tidy
    DOC "clang-tidy static analyzer" OPTIONAL)

if(clang_tidy_exe)
    # ── compile_commands.json ──────────────────────────────────────
    # Negligible cost, enables all clang-based tools.
    set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

    # ── Co-compilation (per-file, during build) ────────────────────
    # -p ${CMAKE_BINARY_DIR}: the generator runs clang-tidy without
    # appending `-- <compile flags>` - the tool resolves the real
    # flags from the compilation database itself.
    # Ref: Professional CMake §32.1.1
    # The project enables both C and CXX - lint both.
    foreach(lang IN ITEMS C CXX)
        set(CMAKE_${lang}_CLANG_TIDY
            "${clang_tidy_exe}" -p "${CMAKE_BINARY_DIR}"
            CACHE STRING "clang-tidy co-compilation command")
    endforeach()

    # ── Copy .clang-tidy into build tree ───────────────────────────
    # Generated sources live in the build dir.  Without a
    # .clang-tidy there, they get no settings (or wrong defaults)
    # when the build dir is outside the source tree.
    # configure_file works correctly even under FetchContent.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
        configure_file(.clang-tidy .clang-tidy COPYONLY)
    endif()

    # ── Standalone target (whole-project, parallel) ────────────────
    # run-clang-tidy uses the compilation database and runs
    # clang-tidy in parallel across all TUs — far faster than
    # a serial custom target, and works with any generator.
    find_program(
        run_clang_tidy_exe
        NAMES run-clang-tidy run-clang-tidy.py
        DOC "run-clang-tidy parallel wrapper")

    if(run_clang_tidy_exe)
        add_custom_target(
            ${PROJECT_NAME}-clang-tidy
            COMMAND
                "${run_clang_tidy_exe}" -clang-tidy-binary "${clang_tidy_exe}"
                -p "${CMAKE_BINARY_DIR}"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            VERBATIM
            COMMENT "running clang-tidy (parallel) on ${PROJECT_NAME}"
            USES_TERMINAL)
    else()
        message(
            NOTICE
            "run-clang-tidy not found -- "
            "'${PROJECT_NAME}-clang-tidy' target unavailable\n"
            "co-compilation via CMAKE_<LANG>_CLANG_TIDY still active")
    endif()
else()
    message(
        NOTICE
        "clang-tidy not found -- static analysis disabled\n"
        "  fedora:  sudo dnf install clang-tools-extra\n"
        "  ubuntu:  sudo apt install clang-tidy\n"
        "  macos:   brew install llvm\n"
        "  windows: choco install llvm")
endif()
