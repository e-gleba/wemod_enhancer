# ─── clang-doc: AST-based documentation from source ───────────────
# Requires compile_commands.json — every clang LibTooling-based tool does.
# clang-doc is still "early development" per LLVM docs; expect rough edges.

find_program(
    clang_doc_exe
    NAMES clang-doc
    DOC "clang-doc: generates C/C++ documentation from AST" OPTIONAL)

if(NOT clang_doc_exe)
    message(
        NOTICE
        "clang-doc not found -- '${PROJECT_NAME}-clang-doc' target disabled\n"
        "  fedora:  sudo dnf install clang-tools-extra\n"
        "  ubuntu:  sudo apt install clang-tools-extra\n"
        "  macos:   brew install llvm\n"
        "  windows: choco install llvm")
    return()
endif()

set(clang_doc_output_dir "${CMAKE_CURRENT_BINARY_DIR}/clang-doc")

# Build a --filter regex that matches only project source dirs.
# This keeps third-party / fetched dependency TUs out of the docs.
set(clang_doc_filter_dirs "")
foreach(dir IN ITEMS src include)
    if(IS_DIRECTORY "${PROJECT_SOURCE_DIR}/${dir}")
        list(APPEND clang_doc_filter_dirs "${PROJECT_SOURCE_DIR}/${dir}")
    endif()
endforeach()
list(
    JOIN
    clang_doc_filter_dirs
    "|"
    clang_doc_filter_regex)

add_custom_target(
    ${PROJECT_NAME}-clang-doc
    COMMAND
        "${clang_doc_exe}"
        # --executor=all-TUs: documented invocation mode for
        # compilation databases.  Without it clang-doc expects
        # positional source file arguments.
        --executor=all-TUs -p "${CMAKE_BINARY_DIR}" --format=html
        "--output=${clang_doc_output_dir}" "--project-name=${PROJECT_NAME}"
        "--source-root=${PROJECT_SOURCE_DIR}"
        # Filter to project sources only — avoids documenting
        # system headers and fetched dependencies.
        "--filter=${clang_doc_filter_regex}" --doxygen --ignore-map-errors
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VERBATIM
    COMMENT "generating clang-doc html for ${PROJECT_NAME}"
    USES_TERMINAL)

include(GNUInstallDirs)
install(
    DIRECTORY "${clang_doc_output_dir}/"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/clang-doc"
    COMPONENT documentation
    OPTIONAL)
