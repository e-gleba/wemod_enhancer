// WeMod Enhancer - desktop app (Dear ImGui + SDL3, C++23).
// Linux font rendering fix: force SDL renderer texture creation after backend init.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

int main_placeholder = 0;
