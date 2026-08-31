if(WEMOD_ENHANCER_BUILD_GUI)
    add_subdirectory(gui)
endif()

# version.dll is a Windows PE. One CMake process = one toolchain:
#   Windows parent     → add_subdirectory (MSVC or Clang GNU driver)
#   Linux GUI parent   → ExternalProject + llvm-mingw
#   llvm-mingw parent  → add_subdirectory (SYSTEM_NAME is already Windows)
# Ref: Professional CMake §27
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_subdirectory(version_dll)
elseif(WEMOD_ENHANCER_BUILD_GUI)
    include(ct_version_dll)
endif()
