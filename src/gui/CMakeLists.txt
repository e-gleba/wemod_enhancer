add_executable(
    wemod_enhancer_gui WIN32 MACOSX_BUNDLE main.cpp app.cpp sdl_platform.cpp
    imgui_view.cpp app.hpp platform.hpp)

target_link_libraries(wemod_enhancer_gui PRIVATE imgui::sdl3_renderer
                                                 Microsoft.GSL::GSL)

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

# Dev builds run straight from the build tree, so stage the runtime
# companions next to the GUI binary: the patcher script it shells out
# to and, on Windows, the version.dll proxy it hands to the patcher.
# $<TARGET_FILE_DIR> follows the per-config subdir of multi-config
# generators, and TARGET_FILE keeps this correct if the proxy output
# name or location ever moves. Install/CPack rules are untouched.
# Ref: https://cmake.org/cmake/help/latest/command/add_custom_command.html
add_custom_command(
    TARGET wemod_enhancer_gui
    POST_BUILD
    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/scripts/wemod_enhancer.py"
        "$<TARGET_FILE_DIR:wemod_enhancer_gui>"
    VERBATIM)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_custom_command(
        TARGET wemod_enhancer_gui
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:wemod_version_dll>"
                "$<TARGET_FILE_DIR:wemod_enhancer_gui>"
        VERBATIM)
endif()
