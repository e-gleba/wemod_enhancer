# gui/CMakeLists.txt
#
# Dear ImGui frontend for the Python patcher CLI. Opt-in via
# WEMOD_ENHANCER_BUILD_GUI (see the top-level CMakeLists.txt).

add_executable(wemod_enhancer_gui main.cpp)

# imgui::sdl3_renderer publicly carries imgui::imgui + SDL3::SDL3.
target_link_libraries(wemod_enhancer_gui PRIVATE imgui::sdl3_renderer)

target_compile_features(wemod_enhancer_gui PRIVATE cxx_std_23)

# Code-generate the build-tree config header with the absolute path to
# the in-tree patcher script, so a dev build finds wemod_enhancer.py
# even though the exe and the script live in different directories.
# (Install layout - script next to the exe - is checked first at
# runtime; this is only the build-tree fallback.)
set(WEMOD_ENHANCER_DEV_SCRIPT "${PROJECT_SOURCE_DIR}/scripts/wemod_enhancer.py")
configure_file(config.hpp.in config.hpp @ONLY)
target_include_directories(wemod_enhancer_gui
                           PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# No console window on Windows (GUI subsystem), .app bundle on macOS.
set_target_properties(wemod_enhancer_gui PROPERTIES WIN32_EXECUTABLE TRUE
                                                    MACOSX_BUNDLE TRUE)

# No C++ modules: CMP0155 would add -fmodule-mapper flags that the
# clang-tidy co-compilation (CXX_CLANG_TIDY) does not understand.
set_target_properties(wemod_enhancer_gui PROPERTIES CXX_SCAN_FOR_MODULES OFF)

# cpplint enforces Google style (same-line braces, no non-const refs),
# which contradicts this project's .clang-format; clang-tidy is the
# configured gate, so disable the cpplint co-compile for this target.
set_target_properties(wemod_enhancer_gui PROPERTIES CXX_CPPLINT "")

# Self-contained binaries: statically link the C++ runtime (and CRT) so
# the executable runs on a bare OS install. SDL3 is already built static
# (see cmake/cpm/sdl3-config.cmake).
if(MSVC)
    set_target_properties(
        wemod_enhancer_gui
        PROPERTIES MSVC_RUNTIME_LIBRARY
                   "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(MINGW)
    # Mirrors src/CMakeLists.txt: static libc++/libgcc for the
    # llvm-mingw cross build.
    target_link_options(wemod_enhancer_gui PRIVATE -static)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Name the artifact wemod_enhancer_gui.elf so the Linux binary is
    # unmistakable next to the Windows wemod_enhancer_gui.exe.
    set_target_properties(wemod_enhancer_gui PROPERTIES SUFFIX ".elf")
    # Static libstdc++/libgcc only when the toolchain ships the static
    # archives (Debian: libstdc++-*-dev, Fedora: libstdc++-static).
    # Otherwise fall back to the system libstdc++, which every base
    # install has - SDL3 stays static either way.
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-static-libstdc++ -static-libgcc"
                            have_static_cxx_runtime)
    if(have_static_cxx_runtime)
        target_link_options(wemod_enhancer_gui
                            PRIVATE -static-libstdc++ -static-libgcc)
    else()
        message(
            STATUS
                "wemod_enhancer_gui: no static libstdc++ found "
                "(fedora: sudo dnf install libstdc++-static) - linking "
                "the C++ runtime dynamically")
    endif()
endif()

# Install next to the DLL and the Python CLI (see src/ and scripts/).
# RUNTIME installs with the executable bit set on Linux.
include(GNUInstallDirs)
install(TARGETS wemod_enhancer_gui RUNTIME)
