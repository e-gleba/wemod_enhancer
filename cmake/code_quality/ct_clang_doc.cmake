# --- clang-doc: AST-based documentation from source ---------------------------
# Requires compile_commands.json - every clang LibTooling-based tool does.
# clang-doc is still "early development" per LLVM docs; expect rough edges.

include_guard(GLOBAL)

find_program(
    clang_doc_exe
    NAMES clang-doc
    DOC "clang-doc: generates C/C++ documentation from AST" OPTIONAL)

if(NOT clang_doc_exe)
    message(NOTICE [[clang-doc not found - clang-doc target disabled
  fedora:  sudo dnf install clang-tools-extra
  ubuntu:  sudo apt install clang-tools-extra
  alt:     sudo apt-get install clang-tools
  macos:   brew install llvm
  windows: choco install llvm]])
    return()
endif()

# --- compile_commands.json ----------------------------------------------------
# Enable here too: this module must work even when clang_tidy.cmake
# (which also enables it) is not included first.
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Only Makefile/Ninja generators emit the database; on any other
# generator the target would configure but always fail.
if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
    message(NOTICE
            "clang-doc target disabled - generator '${CMAKE_GENERATOR}'"
            " does not emit compile_commands.json")
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
    ${PROJECT_NAME}_clang_doc
    COMMAND
        "${clang_doc_exe}"
        # --executor=all-TUs: documented invocation mode for
        # compilation databases.  Without it clang-doc expects
        # positional source file arguments.
        --executor=all-TUs -p "${CMAKE_BINARY_DIR}" --format=html
        "--output=${clang_doc_output_dir}" "--project-name=${PROJECT_NAME}"
        "--source-root=${PROJECT_SOURCE_DIR}"
        # Filter to project sources only - avoids documenting
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
