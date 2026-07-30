file(
    GLOB_RECURSE
    src
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

add_executable(tests ${src})

target_include_directories(tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_features(tests PRIVATE cxx_std_23)

target_link_libraries(tests PRIVATE doctest::doctest)

target_compile_definitions(
    tests
    PRIVATE DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
            DOCTEST_CONFIG_INCLUDE_TYPE_TRAITS
            DOCTEST_CONFIG_REQUIRE_STRINGIFICATION_FOR_ALL_USED_TYPES
            DOCTEST_CONFIG_ASSERTS_RETURN_VALUES
            DOCTEST_CONFIG_USE_STD_HEADERS)

# Disable clang-tidy co-compilation for the tests target.
#
# With Ninja Multi-Config, CMAKE_CXX_CLANG_TIDY co-compilation causes
# clang-tidy to read .modmap response files for ALL configurations
# (Debug, RelWithDebInfo, Release), but only the active configuration's
# .modmap exists.  This produces:
#   error: no such file or directory: '@...Debug\main.cpp.obj.modmap'
#
# The standalone run-clang-tidy target remains available for
# whole-project static analysis.
set_target_properties(tests PROPERTIES CXX_CLANG_TIDY "")

# doctest_discover_tests runs the built executable with --list-
# test-cases to register each test case individually with CTest.
# This only works on native builds where the host can execute the
# binary. When cross-compiling, the executable targets a foreign
# platform and cannot run on the host.
if(NOT CMAKE_CROSSCOMPILING)
    include(doctest)
    doctest_discover_tests(tests)
endif()
