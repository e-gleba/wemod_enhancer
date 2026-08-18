# ImGui frontend for the Python patcher CLI: runs the tested
# tools/wemod_enhancer.py commands and shows their output.
#
# SDL3 and imgui are resolved in cmake/cpm-config.cmake. The
# imgui::sdl3_renderer target links imgui::imgui and a static SDL3
# publicly; combined with the static C++ runtime / CRT below, the
# produced binary is fully self-contained.

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

set_target_properties(
    wemod_enhancer_gui
    PROPERTIES # No console window on Windows; SDL_main.h provides WinMain.
               WIN32_EXECUTABLE ON
               # No C++ modules are used. CMP0155 scanning makes
               # GCC/Clang emit -fmodule-mapper flags that the
               # clang-tidy co-compilation rejects (modmap errors).
               CXX_SCAN_FOR_MODULES OFF
               # cpplint enforces Google style (same-line braces, no
               # non-const references), which conflicts with this
               # project's .clang-format — clang-tidy stays the gate.
               CXX_CPPLINT "")

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

# Static libstdc++/libgcc: the ELF then needs only glibc at runtime
# (X11/Wayland are dlopen'd by SDL3). Probed at configure time — not
# every distro ships the static archives (Fedora splits them into
# libstdc++-static), and a dynamic libstdc++ fallback is fine: like
# glibc, it is part of every base system, Steam Deck included.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-static-libstdc++ -static-libgcc"
                            have_static_cxx_runtime)
    if(have_static_cxx_runtime)
        target_link_options(wemod_enhancer_gui
                            PRIVATE -static-libstdc++ -static-libgcc)
    else()
        message(
            STATUS
                "wemod_enhancer_gui: static libstdc++/libgcc not found — "
                "linking the C++ runtime dynamically.\n"
                "  fedora: sudo dnf install libstdc++-static\n"
                "  ubuntu: ships with libstdc++-*-dev")
    endif()
endif()

include(GNUInstallDirs)
# install(TARGETS) already installs the ELF with the executable
# permission bit set (0755) — no extra PERMISSIONS needed.
install(TARGETS wemod_enhancer_gui
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
