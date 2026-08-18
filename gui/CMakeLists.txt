# ImGui frontend for the Python patcher CLI: runs the tested
# tools/wemod_enhancer.py commands and shows their output.
#
# SDL3 and imgui are resolved in cmake/cpm-config.cmake for every build
# (native and cross). imgui::sdl3_renderer links imgui::imgui and a
# static SDL3 publicly; combined with the static C++ runtime / CRT
# below, the produced binary is fully self-contained.

add_executable(wemod_enhancer_gui main.cpp)

target_compile_features(wemod_enhancer_gui PRIVATE cxx_std_23)

target_link_libraries(wemod_enhancer_gui PRIVATE imgui::sdl3_renderer)

# Lets the GUI fall back to the in-tree CLI (tools/wemod_enhancer.py)
# when it runs from a build tree instead of an install tree.
# Backslashes are normalized so the path is a valid C string literal.
string(REPLACE "\\" "/" wemod_enhancer_source_dir "${CMAKE_SOURCE_DIR}")
target_compile_definitions(
    wemod_enhancer_gui
    PRIVATE WEMOD_ENHANCER_SOURCE_DIR="${wemod_enhancer_source_dir}")

# No console window on Windows; SDL_main.h provides WinMain.
set_target_properties(wemod_enhancer_gui PROPERTIES WIN32_EXECUTABLE ON)

# Static MSVC CRT: the .exe must run on a bare Windows install.
set_target_properties(
    wemod_enhancer_gui
    PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MinGW-Clang needs -static to link static libc++/libgcc; MSVC-ABI Clang
# uses MSVC_RUNTIME_LIBRARY (above) and does not understand -static the
# same way through lld-link. Mirrors the version target in src/.
if(WIN32
   AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
   AND NOT CMAKE_CXX_COMPILER_TARGET MATCHES "msvc")
    target_link_options(wemod_enhancer_gui PRIVATE -static)
endif()

# Static libstdc++/libgcc: the ELF must run on a bare glibc system
# (Steam Deck). X11/Wayland are dlopen'd by SDL3 at runtime.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(wemod_enhancer_gui PRIVATE -static-libstdc++
                                                   -static-libgcc)
endif()

include(GNUInstallDirs)
# install(TARGETS) already installs the ELF with the executable
# permission bit set (0755) — no extra PERMISSIONS needed.
install(TARGETS wemod_enhancer_gui
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
