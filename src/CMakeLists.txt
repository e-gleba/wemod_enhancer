if(WEMOD_ENHANCER_BUILD_GUI)
    add_subdirectory(gui)
endif()

# version.dll is a Windows PE. One CMake process = one toolchain:
#   Windows parent     -> add_subdirectory (MSVC or Clang GNU driver)
#   Linux GUI parent   -> ExternalProject + llvm-mingw
#   llvm-mingw parent  -> add_subdirectory (SYSTEM_NAME is already Windows)
# Ref: Professional CMake §27
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_subdirectory(version_dll)
    if(WEMOD_ENHANCER_BUILD_GUI)
        # The GUI never links the DLL, so a target-limited build
        # (cmake --build --target wemod_enhancer_gui, used by the GUI
        # build presets) would skip it and CPack could not package it.
        # The package ships version.dll next to the exe - make the GUI
        # build depend on it.
        add_dependencies(wemod_enhancer_gui version)
    endif()
elseif(WEMOD_ENHANCER_BUILD_GUI)
    include(ct_version_dll)
    # Without add_dependencies the ExternalProject target never builds:
    # nothing in the parent depends on it, so CPack's install step
    # cannot find version.dll the install(FILES) rule expects.
    add_dependencies(wemod_enhancer_gui wemod_version_dll)
endif()
