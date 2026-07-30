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

# The test sources do not use C++20 modules, but CMake 3.28+ still
# generates .modmap response files when CXX_SCAN_FOR_MODULES is on.
# With Ninja Multi-Config, clang-tidy co-compilation tries to read
# .modmap files for all configurations — only the active one exists.
# Disabling module scanning avoids the spurious .modmap errors.
set_target_properties(tests PROPERTIES CXX_SCAN_FOR_MODULES OFF)

# doctest_discover_tests runs the built executable with --list-
# test-cases to register each test case individually with CTest.
# This only works on native builds where the host can execute the
# binary. When cross-compiling, the executable targets a foreign
# platform and cannot run on the host.
if(NOT CMAKE_CROSSCOMPILING)
    include(doctest)
    doctest_discover_tests(tests)
endif()
