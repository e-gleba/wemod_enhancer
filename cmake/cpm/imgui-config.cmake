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

# NOTE: misc/freetype/imgui_freetype.cpp is deliberately NOT in this
# list — it is added conditionally below, only when FreeType was found.
# Compiling it without FreeType headers fails on #include <ft2build.h>.
target_sources(
    imgui
    PRIVATE ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_demo.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)

target_include_directories(
    imgui SYSTEM PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
                        $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/misc/cpp>)

target_compile_features(imgui PUBLIC cxx_std_23)

# No C++ modules anywhere: CMP0155 scanning would make GCC/Clang emit
# -fmodule-mapper flags that the clang-tidy co-compilation rejects.
set_target_properties(imgui PROPERTIES CXX_SCAN_FOR_MODULES OFF)

# FreeType is resolved before imgui (see cmake/cpm-config.cmake). A CPM
# build tree exports the plain `freetype` target; an installed FreeType
# (CMake's FindFreetype module or freetype's exported config) provides
# Freetype::Freetype. Accept either, preferring the canonical name.
if(TARGET Freetype::Freetype)
    set(imgui_freetype_target Freetype::Freetype)
elseif(TARGET freetype)
    set(imgui_freetype_target freetype)
endif()

if(imgui_freetype_target)
    target_sources(imgui
                   PRIVATE ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp)

    # PUBLIC because imgui_freetype.h exposes FreeType types to consumers.
    target_link_libraries(imgui PUBLIC ${imgui_freetype_target})
    target_include_directories(
        imgui SYSTEM
        PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/misc/freetype>)
    target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
else()
    message(
        STATUS "imgui: FreeType not found — custom font rasterizer disabled.")
endif()

# SDL3 renderer backend: stock imgui_impl_sdl3 + imgui_impl_sdlrenderer3.
# Needs no OpenGL development files — SDL3 loads the platform graphics
# APIs (Direct3D/Vulkan/OpenGL/software) dynamically at runtime, so the
# executable stays self-contained. Works for native and cross builds.
if(TARGET SDL3::SDL3)
    add_library(imgui_sdl3_renderer STATIC)
    add_library(imgui::sdl3_renderer ALIAS imgui_sdl3_renderer)

    target_sources(
        imgui_sdl3_renderer
        PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
                ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)

    # Backends include <imgui.h> and <imgui_impl_*.h>.
    # imgui::imgui already exposes ${imgui_SOURCE_DIR}; we only need the
    # backends directory here.
    target_include_directories(
        imgui_sdl3_renderer SYSTEM
        PUBLIC $<BUILD_INTERFACE:${imgui_SOURCE_DIR}/backends>)

    target_link_libraries(imgui_sdl3_renderer PUBLIC imgui::imgui
                                                     SDL3::SDL3)

    target_compile_features(imgui_sdl3_renderer PUBLIC cxx_std_23)
    set_target_properties(imgui_sdl3_renderer
                          PROPERTIES CXX_SCAN_FOR_MODULES OFF)
else()
    message(
        STATUS
            "imgui: SDL3::SDL3 target missing — skipping SDL3 renderer backend."
        )
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
    set_target_properties(imgui_sdl3_opengl3
                          PROPERTIES CXX_SCAN_FOR_MODULES OFF)
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
