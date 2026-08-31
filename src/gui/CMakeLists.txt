add_executable(wemod_enhancer_gui WIN32 MACOSX_BUNDLE main.cpp)

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
    "$<$<AND:$<PLATFORM_ID:Windows>,$<LINK_LANG_AND_ID:CXX,GNU>>:-static>"
    "$<$<PLATFORM_ID:Linux>:-static-libstdc++;-static-libgcc>")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(wemod_enhancer_gui PROPERTIES SUFFIX ".elf")
endif()

install(
    TARGETS wemod_enhancer_gui
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION . # required because MACOSX_BUNDLE is TRUE
)
