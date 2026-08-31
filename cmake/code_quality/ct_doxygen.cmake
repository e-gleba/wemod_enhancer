# SPDX-FileCopyrightText: 2025 e-gleba
# SPDX-License-Identifier: MIT
#
# Doxygen documentation generation target.
# Optional dependency: Graphviz (dot) for class / call / collaboration /
# directory graphs.

include_guard(GLOBAL)

find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(NOT DOXYGEN_FOUND)
    message(NOTICE [[doxygen not found - documentation target disabled
  fedora:  sudo dnf install doxygen graphviz
  ubuntu:  sudo apt install doxygen graphviz
  alt:     sudo apt-get install doxygen graphviz
  macos:   brew install doxygen graphviz
  windows: choco install doxygen.install graphviz]])
    return()
endif()

# --- centralised paths --------------------------------------------------------
set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated_docs")
set(DOXYGEN_OUTPUT_DIRECTORY "${DOXYGEN_OUTPUT_DIR}")

# --- project metadata ---------------------------------------------------------
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

# --- input / content ----------------------------------------------------------
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

# --- navigation and sorting ---------------------------------------------------
set(DOXYGEN_GENERATE_TREEVIEW YES)
set(DOXYGEN_SEARCHENGINE YES)
set(DOXYGEN_DISABLE_INDEX NO)
set(DOXYGEN_ALPHABETICAL_INDEX YES)
set(DOXYGEN_SORT_MEMBER_DOCS YES)
set(DOXYGEN_SORT_BRIEF_DOCS YES)
set(DOXYGEN_SORT_GROUP_NAMES YES)

# --- cross-references ---------------------------------------------------------
set(DOXYGEN_REFERENCED_BY_RELATION YES)
set(DOXYGEN_REFERENCES_RELATION YES)

# --- quality checklists -------------------------------------------------------
set(DOXYGEN_GENERATE_TODOLIST YES)
set(DOXYGEN_GENERATE_BUGLIST YES)
set(DOXYGEN_GENERATE_DEPRECATEDLIST YES)

# --- parser settings ----------------------------------------------------------
if(NOT CMAKE_CROSSCOMPILING)
    set(DOXYGEN_CLANG_ASSISTED_PARSING YES)
    set(DOXYGEN_CLANG_OPTIONS "-std=c++23 -stdlib=libc++")
endif()
set(DOXYGEN_CPP_CLI_SUPPORT YES)

# --- output formats -----------------------------------------------------------
set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_HTML_OUTPUT html)
set(DOXYGEN_GENERATE_MAN YES)
set(DOXYGEN_MAN_OUTPUT man)

# --- presentation -------------------------------------------------------------
set(DOXYGEN_HTML_DYNAMIC_SECTIONS YES)
set(DOXYGEN_INTERACTIVE_SVG YES)
set(DOXYGEN_USE_MATHJAX YES)
set(DOXYGEN_MATHJAX_FORMAT TeX)

# --- graphs (Graphviz) --------------------------------------------------------
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

# --- warning hygiene ----------------------------------------------------------
set(DOXYGEN_QUIET YES)
set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
set(DOXYGEN_WARN_IF_DOC_ERROR YES)
set(DOXYGEN_WARN_NO_PARAMDOC YES)
set(DOXYGEN_WARN_AS_ERROR NO)

# --- modern theme: Doxygen Awesome --------------------------------------------
# Std CMake FetchContent, self-contained in this module - no CPM, no
# include-order dependency on cmake/cpm.cmake.
# The theme repo has no CMakeLists.txt: MakeAvailable only populates it
# and never calls add_subdirectory (documented behavior) - exactly what
# a download-only CSS dependency needs.
include(FetchContent)
FetchContent_Declare(
    doxygen_awesome
    GIT_REPOSITORY https://github.com/jothepro/doxygen-awesome-css.git
    GIT_TAG v2.4.2
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(doxygen_awesome)

set(DOXYGEN_HTML_EXTRA_STYLESHEET
    "${doxygen_awesome_SOURCE_DIR}/doxygen-awesome.css"
    "${doxygen_awesome_SOURCE_DIR}/doxygen-awesome-sidebar-only.css")
set(DOXYGEN_HTML_COLORSTYLE LIGHT)

# --- target -------------------------------------------------------------------
doxygen_add_docs(docs ${docs_inputs}
                 COMMENT "Generating API documentation with Doxygen")

# --- installation / packaging -------------------------------------------------
include(GNUInstallDirs)
install(
    DIRECTORY "${DOXYGEN_OUTPUT_DIR}/"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
    COMPONENT documentation
    OPTIONAL)
