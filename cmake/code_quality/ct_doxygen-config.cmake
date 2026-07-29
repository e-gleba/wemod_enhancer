# SPDX-FileCopyrightText: 2025 e-gleba
# SPDX-License-Identifier: MIT
#
# Doxygen documentation generation target.
# Optional dependency: Graphviz (dot) for class / call / collaboration /
# directory graphs.

find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(NOT DOXYGEN_FOUND)
    message(
        NOTICE
        "Doxygen not found -- documentation target disabled.\n"
        "Install instructions:\n"
        "  Fedora:  sudo dnf install doxygen graphviz\n"
        "  Ubuntu:  sudo apt install doxygen graphviz\n"
        "  macOS:   brew install doxygen graphviz\n"
        "  Windows: choco install doxygen.install graphviz")
    return()
endif()

# ─── Centralised paths ───────────────────────────────────
set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated_docs")
set(DOXYGEN_OUTPUT_DIRECTORY "${DOXYGEN_OUTPUT_DIR}")

# ─── Project metadata ────────────────────────────────────
set(DOXYGEN_PROJECT_NAME "${PROJECT_NAME}")
set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
set(DOXYGEN_PROJECT_BRIEF "${PROJECT_DESCRIPTION}")
set(DOXYGEN_CREATE_SUBDIRS YES)
set(DOXYGEN_FULL_PATH_NAMES NO)
set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
set(DOXYGEN_MULTILINE_CPP_IS_BRIEF YES)
set(DOXYGEN_INHERIT_DOCS YES)
set(DOXYGEN_TAB_SIZE 4)

# Strip absolute host paths so docs only show repository-relative locations.
set(DOXYGEN_STRIP_FROM_PATH "${PROJECT_SOURCE_DIR}")
set(DOXYGEN_STRIP_FROM_INC_PATH "${PROJECT_SOURCE_DIR}")

# ─── Input / content ─────────────────────────────────────
set(DOXYGEN_RECURSIVE YES)
set(DOXYGEN_EXTRACT_ALL YES)
set(DOXYGEN_EXTRACT_PRIVATE YES)
set(DOXYGEN_EXTRACT_STATIC YES)
set(DOXYGEN_EXTRACT_ANON_NSPACES YES)
set(DOXYGEN_BUILTIN_STL_SUPPORT YES)
set(DOXYGEN_SHOW_INCLUDE_FILES YES)
set(DOXYGEN_SOURCE_BROWSER YES)
set(DOXYGEN_REFERENCES_LINK_SOURCE YES)
set(DOXYGEN_SOURCE_TOOLTIPS YES)
set(DOXYGEN_VERBATIM_HEADERS YES)
set(DOXYGEN_MARKDOWN_SUPPORT YES)
set(DOXYGEN_MARKDOWN_ID_STYLE GITHUB)
set(DOXYGEN_IMPLICIT_DIR_DOCS YES)
set(DOXYGEN_FILE_PATTERNS
    "*.c"
    "*.cc"
    "*.cxx"
    "*.cpp"
    "*.c++"
    "*.h"
    "*.hh"
    "*.hxx"
    "*.hpp"
    "*.h++"
    "*.inc"
    "*.md"
    "*.dox")
set(DOXYGEN_EXCLUDE_PATTERNS "*/build/*" "*/third_party/*" "*/tests/*")

# Collect directories and files to scan.
set(docs_inputs "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/include")

# Promote the top-level readme to the main page (case-insensitive).
foreach(readme_name IN ITEMS "README.md" "readme.md")
    if(EXISTS "${PROJECT_SOURCE_DIR}/${readme_name}")
        list(APPEND docs_inputs "${PROJECT_SOURCE_DIR}/${readme_name}")
        set(DOXYGEN_USE_MDFILE_AS_MAINPAGE
            "${PROJECT_SOURCE_DIR}/${readme_name}")
        break()
    endif()
endforeach()

# Auto-detect a license file so it appears as its own documentation page.
foreach(
    license_name IN
    ITEMS "LICENSE"
          "LICENSE.md"
          "license.md"
          "COPYING"
          "NOTICE"
          "NOTICE.md")
    if(EXISTS "${PROJECT_SOURCE_DIR}/${license_name}")
        list(APPEND docs_inputs "${PROJECT_SOURCE_DIR}/${license_name}")
        break()
    endif()
endforeach()

# Auto-detect an examples directory for embedded tutorials.
foreach(
    example_dir IN
    ITEMS "examples"
          "example"
          "demos"
          "demo")
    if(IS_DIRECTORY "${PROJECT_SOURCE_DIR}/${example_dir}")
        set(DOXYGEN_EXAMPLE_PATH "${PROJECT_SOURCE_DIR}/${example_dir}")
        break()
    endif()
endforeach()

# Auto-detect a project logo.
foreach(
    logo_path IN
    ITEMS "logo.svg"
          "logo.png"
          "docs/logo.svg"
          "docs/logo.png")
    if(EXISTS "${PROJECT_SOURCE_DIR}/${logo_path}")
        set(DOXYGEN_PROJECT_LOGO "${PROJECT_SOURCE_DIR}/${logo_path}")
        break()
    endif()
endforeach()

# ─── Navigation & sorting ────────────────────────────────
set(DOXYGEN_GENERATE_TREEVIEW YES)
set(DOXYGEN_SEARCHENGINE YES)
set(DOXYGEN_DISABLE_INDEX NO)
set(DOXYGEN_ALPHABETICAL_INDEX YES)
set(DOXYGEN_SORT_MEMBER_DOCS YES)
set(DOXYGEN_SORT_BRIEF_DOCS YES)
set(DOXYGEN_SORT_GROUP_NAMES YES)

# ─── Cross-references ────────────────────────────────────
set(DOXYGEN_REFERENCED_BY_RELATION YES)
set(DOXYGEN_REFERENCES_RELATION YES)

# ─── Quality checklists ──────────────────────────────────
set(DOXYGEN_GENERATE_TODOLIST YES)
set(DOXYGEN_GENERATE_BUGLIST YES)
set(DOXYGEN_GENERATE_DEPRECATEDLIST YES)

# ─── Parser settings ─────────────────────────────────────
if(NOT CMAKE_CROSSCOMPILING)
    set(DOXYGEN_CLANG_ASSISTED_PARSING YES)
    set(DOXYGEN_CLANG_OPTIONS "-std=c++23 -stdlib=libc++")
endif()
set(DOXYGEN_CPP_CLI_SUPPORT YES)

# ─── Output formats ──────────────────────────────────────
set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_HTML_OUTPUT html)
set(DOXYGEN_GENERATE_MAN YES)
set(DOXYGEN_MAN_OUTPUT man)

# ─── Presentation ────────────────────────────────────────
set(DOXYGEN_HTML_COLORSTYLE "dark")
set(DOXYGEN_HTML_DYNAMIC_SECTIONS YES)
set(DOXYGEN_INTERACTIVE_SVG YES)
set(DOXYGEN_USE_MATHJAX YES)
set(DOXYGEN_MATHJAX_FORMAT TeX)

# ─── Graphs (Graphviz) ───────────────────────────────────
if(DOXYGEN_DOT_FOUND)
    set(DOXYGEN_HAVE_DOT YES)
    set(DOXYGEN_DOT_IMAGE_FORMAT svg)
    set(DOXYGEN_DOT_MULTI_TARGETS YES)
    set(DOXYGEN_DOT_NUM_THREADS 0)
    set(DOXYGEN_CLASS_GRAPH YES)
    set(DOXYGEN_COLLABORATION_GRAPH YES)
    set(DOXYGEN_CALL_GRAPH YES)
    set(DOXYGEN_CALLER_GRAPH YES)
    set(DOXYGEN_UML_LOOK YES)
    set(DOXYGEN_DOT_UML_DETAILS YES)
    set(DOXYGEN_DOT_WRAP_THRESHOLD 100)
    set(DOXYGEN_TEMPLATE_RELATIONS YES)
    set(DOXYGEN_GENERATE_LEGEND YES)
    set(DOXYGEN_DIRECTORY_GRAPH YES)
endif()

# ─── Warning hygiene ─────────────────────────────────────
set(DOXYGEN_QUIET YES)
set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
set(DOXYGEN_WARN_IF_DOC_ERROR YES)
set(DOXYGEN_WARN_NO_PARAMDOC YES)
set(DOXYGEN_WARN_AS_ERROR NO)

# ─── Modern theme: Doxygen Awesome ───────────────────────
include(FetchContent)
fetchcontent_declare(
    doxygen-awesome
    GIT_REPOSITORY https://github.com/jothepro/doxygen-awesome-css.git
    GIT_TAG v2.3.4
    GIT_SHALLOW TRUE
    GIT_PROGRESS FALSE)
fetchcontent_makeavailable(doxygen-awesome)

set(DOXYGEN_HTML_EXTRA_STYLESHEET
    "${doxygen-awesome_SOURCE_DIR}/doxygen-awesome.css"
    "${doxygen-awesome_SOURCE_DIR}/doxygen-awesome-sidebar-only.css")
set(DOXYGEN_HTML_COLORSTYLE LIGHT)

# ─── Target ──────────────────────────────────────────────
doxygen_add_docs(docs ${docs_inputs}
                 COMMENT "Generating API documentation with Doxygen")

# ─── Installation / packaging ──────────────────────────
include(GNUInstallDirs)
install(
    DIRECTORY "${DOXYGEN_OUTPUT_DIR}/"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    COMPONENT documentation
    OPTIONAL)
