# GUI, proxy DLL and patcher script all install into the same bindir.
add_subdirectory(gui)

# version.dll is a Windows PE; one CMake process = one toolchain. Its
# CMakeLists owns the dispatch: native library in a Windows process,
# nested llvm-mingw ExternalProject everywhere else.
add_subdirectory(version_dll)

# Target-limited builds must still produce the DLL for CPack.
add_dependencies(wemod_enhancer_gui wemod_version_dll)
