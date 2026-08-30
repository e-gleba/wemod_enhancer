add_library(version SHARED)
target_sources(version PRIVATE fuses.c library.c library.def)

target_compile_features(version PRIVATE c_std_11)

set_target_properties(
    version
    PROPERTIES PREFIX ""
               MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MinGW (LLVM-MinGW cross): link the GCC runtime statically so the DLL is
# self-contained. MSVC-ABI Clang takes MSVC_RUNTIME_LIBRARY above instead.
if(MINGW)
    target_link_options(version PRIVATE -static)
endif()

install(
    TARGETS version
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" # MSVC import .lib
)
