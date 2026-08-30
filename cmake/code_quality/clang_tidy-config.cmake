# ─── clang-tidy: static analysis ──────────────────────────────────
# Two integration modes:
#   1. Co-compilation  — CMAKE_CXX_CLANG_TIDY (Makefiles/Ninja only)
#   2. Standalone      — run-clang-tidy wrapper target (any generator)
#
# Both use compile_commands.json so clang-tidy sees the *real*
# compiler flags. Settings live in .clang-tidy (YAML), not on the
# command line.

find_program(
    clang_tidy_exe
    NAMES clang-tidy
    DOC "clang-tidy static analyzer" OPTIONAL)

if(clang_tidy_exe)
    # Negligible cost, enables all clang-based tools.
    set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

    # -p ${CMAKE_BINARY_DIR}: point clang-tidy at the compilation
    # database so it resolves the correct toolchain headers.
    set(CMAKE_CXX_CLANG_TIDY
        "${clang_tidy_exe}" -p "${CMAKE_BINARY_DIR}"
        CACHE STRING "clang-tidy co-compilation command")

    # Generated sources live in the build tree; without a .clang-tidy
    # there they get wrong defaults when the build dir is out-of-source.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
        configure_file(.clang-tidy .clang-tidy COPYONLY)
    endif()

    # run-clang-tidy parallelizes across all TUs via the database.
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
            "co-compilation via CMAKE_CXX_CLANG_TIDY still active")
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
