file(
    GLOB_RECURSE
    src
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    add_library(tests SHARED ${src})
else()
    add_executable(tests ${src})
    set_source_files_properties(doctest_android_jni.cpp
                                PROPERTIES HEADER_FILE_ONLY TRUE)
endif()

target_include_directories(tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_features(tests PRIVATE cxx_std_23)

target_link_libraries(tests PRIVATE doctest::doctest
                                    $<$<PLATFORM_ID:Android>:log>)

target_compile_definitions(
    tests
    PRIVATE DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
            DOCTEST_CONFIG_INCLUDE_TYPE_TRAITS
            DOCTEST_CONFIG_REQUIRE_STRINGIFICATION_FOR_ALL_USED_TYPES
            DOCTEST_CONFIG_ASSERTS_RETURN_VALUES
            DOCTEST_CONFIG_USE_STD_HEADERS)

# doctest_discover_tests runs the built executable with --list-
# test-cases to register each test case individually with CTest.
# This only works on native builds where the host can execute the
# binary. When cross-compiling, the executable targets a foreign
# platform and cannot run on the host.
if(NOT CMAKE_CROSSCOMPILING)
    include(doctest)
    doctest_discover_tests(tests)
endif()
