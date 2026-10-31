add_library(wemod_patcher STATIC asar.cpp bundles.cpp patcher.cpp)
add_library(wemod::patcher ALIAS wemod_patcher)

target_compile_features(wemod_patcher PUBLIC cxx_std_23)

target_include_directories(
    wemod_patcher PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                         $<INSTALL_INTERFACE:include>)

target_sources(
    wemod_patcher
    PUBLIC FILE_SET HEADERS BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
           asar.hpp bundles.hpp patcher.hpp)

target_link_libraries(wemod_patcher PRIVATE nlohmann_json::nlohmann_json
                                            picosha2::picosha2 warnings)

# LLVM-style command line over the patcher library.
add_library(wemod_cli STATIC cli.cpp)
add_library(wemod::cli ALIAS wemod_cli)

target_compile_features(wemod_cli PUBLIC cxx_std_23)

target_include_directories(
    wemod_cli PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                     $<INSTALL_INTERFACE:include>)

target_sources(wemod_cli PUBLIC FILE_SET HEADERS
                                BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES cli.hpp)

target_link_libraries(wemod_cli PUBLIC wemod::patcher PRIVATE cxxopts::cxxopts
                                                              warnings)

target_compile_definitions(wemod_cli
                           PUBLIC WEMOD_ENHANCER_VERSION="${PROJECT_VERSION}")

add_executable(wemod_enhancer cli_main.cpp)
target_link_libraries(wemod_enhancer PRIVATE wemod::cli warnings)

# Static CRT on MSVC; fully static binary on MinGW/Clang-Windows, static
# runtimes on Linux. Matches the GUI target below so both binaries ship
# with no external runtime dependency.
target_link_options(
    wemod_enhancer
    PRIVATE
        "$<$<AND:$<PLATFORM_ID:Windows>,$<LINK_LANG_AND_ID:CXX,GNU,Clang>>:-static>"
        "$<$<PLATFORM_ID:Linux>:-static-libstdc++;-static-libgcc>")

set_target_properties(
    wemod_enhancer wemod_patcher wemod_cli
    PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

install(
    TARGETS wemod_enhancer wemod_patcher wemod_cli
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    FILE_SET HEADERS DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/wemod_enhancer")
