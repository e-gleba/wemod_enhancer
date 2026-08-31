# Every configure builds the full self-contained package: the GUI, the
# Windows PE proxy DLL and (see scripts/) the Python patcher all land in
# the same bindir - the GUI finds its patcher next to the executable.
add_subdirectory(gui)

# version.dll is a Windows PE. One CMake process = one toolchain:
#   Windows parent     -> add_subdirectory (MSVC or Clang GNU driver)
#   Linux host         -> ExternalProject + llvm-mingw (ct_version_dll)
#   llvm-mingw parent  -> add_subdirectory (SYSTEM_NAME is already Windows)
# Ref: Professional CMake §27
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_subdirectory(version_dll)
    # The GUI never links the DLL, so a target-limited build
    # (cmake --build --target wemod_enhancer_gui) would skip it and CPack
    # could not package it. The package ships version.dll next to the
    # exe - make the GUI build depend on it.
    add_dependencies(wemod_enhancer_gui version)
else()
    include(ct_version_dll)
    # Without add_dependencies a target-limited build never builds the
    # ExternalProject: nothing else in the parent depends on it, so
    # CPack's install step cannot find the version.dll the
    # install(FILES) rule expects.
    add_dependencies(wemod_enhancer_gui wemod_version_dll)
endif()
