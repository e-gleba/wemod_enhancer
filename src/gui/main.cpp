// main.cpp - SDL3 app callbacks (SDL_MAIN_USE_CALLBACKS).
// Thin glue only: init -> state -> per-frame poll + view::draw + present.
// No widgets here, no filesystem probing, no shell commands. All SDL
// failures are checked via backend::check() and fatal init failures
// pop an SDL message box (see backend_sdl3.cpp) so a broken video
// driver is visible instead of a silent exit.

#include "app.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <memory>

namespace
{

using wemod::gui::AppState;
using wemod::gui::backend::check;
using wemod::gui::backend::pick_window_size;
using wemod::gui::backend::show_error;

[[nodiscard]] AppState* state_of(void* appstate) noexcept
{
    return static_cast<AppState*>(appstate);
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    if (appstate == nullptr) {
        return SDL_APP_FAILURE;
    }
    *appstate = nullptr;

    // SDL_AppQuit runs even when init fails and calls SDL_Quit there -
    // no cleanup needed in this scope.
    if (!check(SDL_Init(SDL_INIT_VIDEO), "SDL_Init", nullptr, true)) {
        return SDL_APP_FAILURE;
    }

    const auto [win_w, win_h]{pick_window_size()};

    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    if (!check(SDL_CreateWindowAndRenderer(
                   "WeMod Enhancer", win_w, win_h,
                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                   &window, &renderer),
               "SDL_CreateWindowAndRenderer", nullptr, true)) {
        return SDL_APP_FAILURE;
    }
    SDL_SetWindowMinimumSize(window, wemod::gui::window_min_width,
                             wemod::gui::window_min_height);
    if (!check(SDL_SetRenderVSync(renderer, 1) == 0, "SDL_SetRenderVSync",
                nullptr, false)) {
        // Non-fatal: continue without vsync.
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Density only: default dark colors stay. Padding stays 1x in
    // window coordinates. SDL_SetRenderScale (DisplayFramebufferScale)
    // maps those onto the HiDPI framebuffer every frame. Do not bake
    // FontScaleDpi / ScaleAllSizes on top - that double-counts (~2x).
    ImGuiStyle& style{ImGui::GetStyle()};
    style.WindowPadding = ImVec2(16.0F, 14.0F);
    style.FramePadding = ImVec2(14.0F, 8.0F);
    style.ItemSpacing = ImVec2(10.0F, 8.0F);
    style.ItemInnerSpacing = ImVec2(8.0F, 6.0F);
    style.ScrollbarSize = 16.0F;
    style.GrabMinSize = 14.0F;

    if (!check(ImGui_ImplSDL3_InitForSDLRenderer(window, renderer),
               "ImGui_ImplSDL3_InitForSDLRenderer", nullptr, true)) {
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return SDL_APP_FAILURE;
    }
    if (!check(ImGui_ImplSDLRenderer3_Init(renderer),
               "ImGui_ImplSDLRenderer3_Init", nullptr, true)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return SDL_APP_FAILURE;
    }

    try {
        auto owner{std::make_unique<AppState>()};
        AppState& state{*owner};
        state.window = window;
        state.renderer = renderer;
        state.install_dir = wemod::gui::core::default_install_dir();
        state.script_path = wemod::gui::core::bundled_script().string();
        state.version_dll =
            wemod::gui::core::bundled_version_dll().string();
        state.python = std::string(wemod::gui::default_python);

        if (const wemod::gui::fs::path app{
                wemod::gui::core::resolve_wemod_dir(state.install_dir)};
            !app.empty()) {
            state.install_dir = app.string();
            state.append_log("auto-detected WeMod install: " +
                             state.install_dir + "\n\n");
        }
        if (wemod::gui::fs::is_regular_file(state.script_path)) {
            state.append_log("using bundled patcher: " + state.script_path +
                             "\n\n");
        } else {
            state.append_log(
                "error: wemod_enhancer.py is missing next to the exe:\n  " +
                state.script_path +
                "\n  fix: re-download the GUI package from the GitHub "
                "releases and unpack the whole folder - it is "
                "self-contained.\n\n");
        }
        state.start_probe();

        *appstate = owner.release();
    } catch (const std::exception& ex) {
        show_error("WeMod Enhancer",
                   std::string("Failed to start the GUI: ") + ex.what(),
                   nullptr);
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return SDL_APP_FAILURE;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    if (event == nullptr) {
        return SDL_APP_CONTINUE;
    }
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    (void)appstate;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    AppState* state{state_of(appstate)};
    if (state == nullptr || state->renderer == nullptr) {
        return SDL_APP_FAILURE;
    }

    wemod::gui::core::poll(*state);

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    wemod::gui::view::draw(*state);

    ImGui::Render();
    // The only DPI knob: map imgui window coordinates onto the
    // HIGH_PIXEL_DENSITY framebuffer. The SDL_Renderer backend skips
    // its own clip-scale when a render scale is set, which also keeps
    // InputText hints from clipping.
    const ImGuiIO& io{ImGui::GetIO()};
    check(SDL_SetRenderScale(state->renderer, io.DisplayFramebufferScale.x,
                             io.DisplayFramebufferScale.y) == 0,
          "SDL_SetRenderScale", nullptr, false);
    const ImVec4& clear{wemod::gui::view::frame_clear_color()};
    check(SDL_SetRenderDrawColorFloat(state->renderer, clear.x, clear.y,
                                      clear.z, clear.w) == 0,
          "SDL_SetRenderDrawColorFloat", nullptr, false);
    check(SDL_RenderClear(state->renderer) == 0, "SDL_RenderClear",
          nullptr, false);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),
                                          state->renderer);
    check(SDL_RenderPresent(state->renderer) == 0, "SDL_RenderPresent",
          nullptr, false);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    (void)result;
    const std::unique_ptr<AppState> owner{state_of(appstate)};

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (owner) {
        if (owner->renderer != nullptr) {
            SDL_DestroyRenderer(owner->renderer);
        }
        if (owner->window != nullptr) {
            SDL_DestroyWindow(owner->window);
        }
    }
    SDL_Quit();
}
