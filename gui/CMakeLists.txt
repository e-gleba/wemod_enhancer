# Dear ImGui frontend for the Python patcher CLI (opt-in via
# WEMOD_ENHANCER_BUILD_GUI). Fully relocatable: the patcher script is
# downloaded from the GitHub releases at runtime, never looked up on disk.

add_executable(wemod_enhancer_gui main.cpp)

# imgui::sdl3_renderer publicly carries imgui::imgui + SDL3::SDL3;
# Microsoft.GSL::GSL is header-only (not_null / finally / Expects).
target_link_libraries(
    wemod_enhancer_gui PRIVATE imgui::sdl3_renderer Microsoft.GSL::GSL)

target_compile_features(wemod_enhancer_gui PRIVATE cxx_std_23)

# SDL_MAIN_USE_CALLBACKS is defined here, not in the source, to keep the TU
# macro-free (cppcoreguidelines-macro-usage). The version definition makes a
# packaged binary report exactly the project() version it was built from.
target_compile_definitions(
    wemod_enhancer_gui
    PRIVATE SDL_MAIN_USE_CALLBACKS=1
            WEMOD_ENHANCER_GUI_VERSION="${PROJECT_VERSION}")

# No console window on Windows (GUI subsystem), .app bundle on macOS.
set_target_properties(wemod_enhancer_gui PROPERTIES WIN32_EXECUTABLE TRUE
                                                    MACOSX_BUNDLE TRUE)

# cpplint enforces Google style, which contradicts this project's
# .clang-format; clang-tidy is the configured gate.
set_target_properties(wemod_enhancer_gui PROPERTIES CXX_CPPLINT "")

# Self-contained binaries: statically link the C++ runtime (and CRT) so the
# executable runs on a bare OS install. SDL3 is already built static
# (cmake/cpm/sdl3-config.cmake).
if(MSVC)
    set_target_properties(
        wemod_enhancer_gui
        PROPERTIES MSVC_RUNTIME_LIBRARY
                   "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(MINGW)
    # Mirrors src/CMakeLists.txt: static libc++/libgcc for llvm-mingw.
    target_link_options(wemod_enhancer_gui PRIVATE -static)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Name the artifact .elf so it is unmistakable next to the Windows .exe.
    set_target_properties(wemod_enhancer_gui PROPERTIES SUFFIX ".elf")
    # Static libstdc++/libgcc only when the toolchain ships the static
    # archives (debian: libstdc++-*-dev, fedora: libstdc++-static).
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

install(TARGETS wemod_enhancer_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
