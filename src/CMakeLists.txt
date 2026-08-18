add_library(version SHARED fuses.c library.c library.def)

set_target_properties(
    version
    PROPERTIES PREFIX ""
               OUTPUT_NAME "version"
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_compile_features(version PRIVATE c_std_11)

# MinGW-Clang needs -static to link static libgcc; MSVC-ABI Clang uses
# MSVC_RUNTIME_LIBRARY (above) for static CRT and does not understand -static
# the same way through lld-link.
if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND NOT CMAKE_C_COMPILER_TARGET MATCHES "msvc")
    target_link_options(version PRIVATE -static)
endif()

include(GNUInstallDirs)
install(
    TARGETS version
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" # MSVC import .lib
)

# Ship the PDB next to the DLL so RelWithDebInfo crashes inside the
# proxy stay debuggable (users can send a stack trace, maintainers can
# debug a release build). install(CODE) checks existence at install
# time: toolchains without PDB support (llvm-mingw) skip silently.
if(WIN32)
    install(
        CODE "if(EXISTS \"$<TARGET_PDB_FILE:version>\")
                  file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\"
                       TYPE FILE FILES \"$<TARGET_PDB_FILE:version>\")
              endif()")
endif()
