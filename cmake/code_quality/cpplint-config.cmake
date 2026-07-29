find_program(
    cpplint_exe
    NAMES cpplint
    DOC "cpplint: google C++ style checker" OPTIONAL)

if(cpplint_exe)
    # ─── Co-compilation: per-file lint on every build (Makefiles/Ninja only) ───
    # CMAKE_CXX_CPPLINT integrates cpplint into the compile step.
    # Only fires for Makefile/Ninja generators; silently ignored elsewhere.
    # Ref: Professional CMake §32.3, cmake.org CXX_CPPLINT property.
    set(CMAKE_CXX_CPPLINT
        "${cpplint_exe}"
        CACHE STRING "cpplint command line for co-compilation linting")

    # ─── Standalone target: lint the whole tree on demand ──────────────────
    # Useful for CI gates and quick sweeps independent of generator.
    # Collects sources explicitly — no file(GLOB).
    set(cpplint_dirs "${PROJECT_SOURCE_DIR}/src"
                     "${PROJECT_SOURCE_DIR}/include")

    set(cpplint_sources "")
    foreach(dir IN LISTS cpplint_dirs)
        if(IS_DIRECTORY "${dir}")
            file(
                GLOB_RECURSE
                dir_sources
                "${dir}/*.h"
                "${dir}/*.hpp"
                "${dir}/*.cpp"
                "${dir}/*.cxx"
                "${dir}/*.cc")
            list(APPEND cpplint_sources ${dir_sources})
        endif()
    endforeach()

    if(cpplint_sources)
        add_custom_target(
            ${PROJECT_NAME}-cpplint
            COMMAND "${cpplint_exe}" "--recursive" "--quiet" ${cpplint_sources}
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            VERBATIM
            COMMENT "running cpplint on ${PROJECT_NAME} sources"
            USES_TERMINAL
            SOURCES ${cpplint_sources}) # IDE visibility
    endif()
else()
    message(
        NOTICE
        "cpplint not found -- '${PROJECT_NAME}-cpplint' target disabled\n"
        "  install: pip install cpplint | sudo dnf install cpplint | brew install cpplint"
    )
endif()
