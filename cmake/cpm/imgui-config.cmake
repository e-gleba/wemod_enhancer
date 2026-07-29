cpmaddpackage(
    NAME
    imgui
    VERSION
    1.92.7
    GITHUB_REPOSITORY
    ocornut/imgui
    EXCLUDE_FROM_ALL
    ON
    DOWNLOAD_ONLY
    TRUE)

add_library(imgui STATIC)
add_library(imgui::imgui ALIAS imgui)

target_sources(
    imgui
    PRIVATE ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_demo.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
            ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)

target_include_directories(
    imgui SYSTEM PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
                        $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/misc/cpp>)

target_compile_features(imgui PUBLIC cxx_std_23)

if(Freetype_FOUND)
    if(NOT TARGET Freetype::Freetype)
        message(
            FATAL_ERROR
                "find_package(Freetype) succeeded but the imported target "
                "Freetype::Freetype is missing. Your FreeType install may be "
                "too old or its CMake config is incomplete.")
    endif()

    target_sources(imgui
                   PRIVATE ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)

    # PUBLIC because imgui_freetype.h exposes FreeType types to consumers.
    target_link_libraries(imgui PUBLIC Freetype::Freetype)
    target_include_directories(
        imgui SYSTEM
        PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/misc/freetype>)
    target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
else()
    message(
        STATUS "imgui: FreeType not found — custom font rasterizer disabled.")
endif()

# Platform packages needed:
#   Windows : vcpkg install opengl --triplet=x64-windows
#   Fedora  : sudo dnf install mesa-libGL-devel mesa-libGLU-devel
#   Arch    : sudo pacman -S mesa glu
#   Ubuntu  : sudo apt-get install libgl1-mesa-dev libglu1-mesa-dev
#   macOS   : OpenGL.framework is included in the SDK

if(TARGET SDL3::SDL3 AND TARGET OpenGL::GL)
    add_library(imgui_sdl3_opengl3 STATIC)
    add_library(imgui::sdl3_opengl3 ALIAS imgui_sdl3_opengl3)

    target_sources(
        imgui_sdl3_opengl3
        PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
                ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)

    # Backends include <imgui.h> and <imgui_impl_*.h>.
    # imgui::imgui already exposes ${imgui_SOURCE_DIR}; we only need the
    # backends directory here.
    target_include_directories(
        imgui_sdl3_opengl3 SYSTEM
        PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/backends>)

    target_link_libraries(imgui_sdl3_opengl3 PUBLIC imgui::imgui SDL3::SDL3
                                                    OpenGL::GL)

    target_compile_features(imgui_sdl3_opengl3 PUBLIC cxx_std_23)
else()
    if(NOT TARGET SDL3::SDL3)
        message(
            STATUS
                "imgui: SDL3::SDL3 target missing — skipping SDL3+OpenGL3 backend."
        )
    endif()
    if(NOT TARGET OpenGL::GL)
        message(
            STATUS
                "imgui: OpenGL::GL target missing — skipping SDL3+OpenGL3 backend."
        )
    endif()
endif()
