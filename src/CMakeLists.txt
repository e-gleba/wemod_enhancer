if(WEMOD_ENHANCER_BUILD_GUI)
    add_subdirectory(gui)
endif()

# version.dll is a Windows PE. One CMake process = one toolchain, so:
#   Windows parent  → add_subdirectory (MSVC or Clang GNU driver)
#   Linux GUI parent → ExternalProject + llvm-mingw (ct_version_dll)
#   Linux CLI parent → unreachable: CLI presets already cross-compile
#                      via CMAKE_TOOLCHAIN_FILE, so they hit the Windows
#                      branch above.
# Ref: Professional CMake §27
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_subdirectory(version_dll)
elseif(WEMOD_ENHANCER_BUILD_GUI)
    include(ct_version_dll)
endif()
