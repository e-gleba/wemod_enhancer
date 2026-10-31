add_executable(
    wemod_enhancer_gui WIN32 MACOSX_BUNDLE main.cpp app.cpp cli.cpp
    sdl_platform.cpp imgui_view.cpp app.hpp cli.hpp meta.hpp platform.hpp)

target_link_libraries(wemod_enhancer_gui PRIVATE imgui::sdl3_renderer
                                                 Microsoft.GSL::GSL
                                                 cxxopts::cxxopts)

target_compile_features(wemod_enhancer_gui PRIVATE cxx_std_23)

target_compile_definitions(
    wemod_enhancer_gui PRIVATE SDL_MAIN_USE_CALLBACKS=1
                               WEMOD_ENHANCER_GUI_VERSION="${PROJECT_VERSION}")

set_target_properties(
    wemod_enhancer_gui PROPERTIES MSVC_RUNTIME_LIBRARY
                                  "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_options(
    wemod_enhancer_gui
    PRIVATE
    "$<$<AND:$<PLATFORM_ID:Windows>,$<LINK_LANG_AND_ID:CXX,GNU,Clang>>:-static>"
    "$<$<PLATFORM_ID:Linux>:-static-libstdc++;-static-libgcc>")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(wemod_enhancer_gui PROPERTIES SUFFIX ".elf")
endif()

install(
    TARGETS wemod_enhancer_gui
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION .)

# Dev builds run straight from the build tree: stage the patcher script
# next to the GUI binary so no install/CPack step is needed to try a
# change. The GUI resolves it relative to its own directory (SDL_AppInit).
# $<TARGET_FILE_DIR> follows the per-config subdir of multi-config
# generators; copy to an explicit file path after make_directory so a
# missing output directory is never mistaken for the file itself
# (MSVC MSB3191); copy_if_different keeps repeat builds silent.
# Install rules stay untouched.
# Ref: https://cmake.org/cmake/help/latest/command/add_custom_command.html
add_custom_command(
    TARGET wemod_enhancer_gui
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:wemod_enhancer_gui>"
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_LIST_DIR}/../../scripts/wemod_enhancer.py"
        "$<TARGET_FILE_DIR:wemod_enhancer_gui>/wemod_enhancer.py"
    VERBATIM)
