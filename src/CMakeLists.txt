# GUI, proxy DLL and patcher script all install into the same bindir.
add_subdirectory(gui)

# version.dll is a Windows PE; one CMake process = one toolchain:
# Windows/llvm-mingw parent -> subdirectory, Linux host -> ExternalProject.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_subdirectory(version_dll)
else()
    include(ct_version_dll)
endif()

# Target-limited builds must still produce the DLL for CPack.
add_dependencies(wemod_enhancer_gui wemod_version_dll)
