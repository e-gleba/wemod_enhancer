# Build the version.dll proxy as a C++23 shared library with GSL
# statically linked and the C runtime statically linked.

add_library(version SHARED fuses.cpp library.cpp library.def)

set_target_properties(
    version
    PROPERTIES PREFIX ""
               OUTPUT_NAME "version"
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_features(version PRIVATE cxx_std_23)

# Fetch GSL (Guidelines Support Library) via CPM and link it statically.
CPMAddPackage("gh:gsl-lite/gsl-lite#0.42.0")
target_link_libraries(version PRIVATE GSL::gsl)

# MinGW-Clang needs -static to link static libgcc/libstdc++; MSVC-ABI
# Clang uses MSVC_RUNTIME_LIBRARY (above) for static CRT and does not
# understand -static the same way through lld-link.
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
