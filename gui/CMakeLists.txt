# ImGui frontend for the Python patcher CLI: runs the tested
# tools/wemod_enhancer.py commands and shows their output.
#
# OpenGL, SDL3 and imgui are resolved in cmake/cpm-config.cmake, which
# skips them when cross-compiling — the root CMakeLists.txt only adds
# this directory for native builds, so imgui::sdl3_opengl3 (which links
# imgui::imgui, SDL3::SDL3 and OpenGL::GL publicly) always exists here.

add_executable(wemod_enhancer_gui main.cpp)

target_compile_features(wemod_enhancer_gui PRIVATE cxx_std_23)

target_link_libraries(wemod_enhancer_gui PRIVATE imgui::sdl3_opengl3)

# Lets the GUI fall back to the in-tree CLI (tools/wemod_enhancer.py)
# when it runs from a build tree instead of an install tree.
# Backslashes are normalized so the path is a valid C string literal.
string(REPLACE "\\" "/" wemod_enhancer_source_dir "${CMAKE_SOURCE_DIR}")
target_compile_definitions(
    wemod_enhancer_gui
    PRIVATE WEMOD_ENHANCER_SOURCE_DIR="${wemod_enhancer_source_dir}")

# No console window on Windows; SDL_main.h provides WinMain.
set_target_properties(wemod_enhancer_gui PROPERTIES WIN32_EXECUTABLE ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Find the bundled libSDL3.so next to the installed executable.
    set_target_properties(wemod_enhancer_gui PROPERTIES INSTALL_RPATH
                                                          "$ORIGIN")
endif()

include(GNUInstallDirs)
install(TARGETS wemod_enhancer_gui
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

# SDL3 is built as a shared library with SDL_INSTALL OFF — ship it next
# to the executable so the install tree is self-contained.
install(FILES "$<TARGET_FILE:SDL3::SDL3>"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
