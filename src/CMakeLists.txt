# Windows version.dll proxy. Forwarded exports live in library.def; C++ only
# contains process-attach behavior.

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

add_library(
    version SHARED
    dll_main.cpp
    fuses.cpp
    fuses.hpp
    version_proxy.cpp
    library.def)

target_compile_features(version PRIVATE cxx_std_23)
target_compile_definitions(version PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(
    version
    PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>")
target_link_libraries(version PRIVATE GSL)
target_link_options(
    version
    PRIVATE
        $<$<AND:$<CXX_COMPILER_ID:Clang>,$<NOT:$<CXX_COMPILER_FRONTEND_VARIANT:MSVC>>>:-static>)

set_target_properties(
    version
    PROPERTIES PREFIX ""
               OUTPUT_NAME "version"
               CXX_EXTENSIONS OFF
               CXX_SCAN_FOR_MODULES OFF
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(GNUInstallDirs)
install(
    TARGETS version
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
