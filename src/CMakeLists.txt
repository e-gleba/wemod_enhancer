# Build the Windows version.dll proxy with a static C runtime.

add_library(
    version SHARED
    dll_main.cpp
    fuses.cpp
    version_proxy.cpp
    proxy.hpp
    library.def)

set_target_properties(
    version
    PROPERTIES PREFIX ""
               OUTPUT_NAME "version"
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_features(version PRIVATE cxx_std_23)

CPMAddPackage("gh:gsl-lite/gsl-lite#0.42.0")
target_link_libraries(version PRIVATE gsl::gsl-lite)

# MinGW-Clang needs static libgcc/libstdc++; MSVC-ABI Clang uses the
# MSVC_RUNTIME_LIBRARY property above instead.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_CXX_COMPILER_TARGET MATCHES "msvc")
    target_link_options(version PRIVATE -static)
endif()

include(GNUInstallDirs)
install(
    TARGETS version
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" # MSVC import .lib
)
