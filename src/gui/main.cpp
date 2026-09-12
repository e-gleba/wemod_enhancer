// main.cpp - SDL3 app callbacks (SDL_MAIN_USE_CALLBACKS).
// Thin glue only: init -> state -> per-frame poll + view::draw + present.
// No widgets here, no filesystem probing, no shell commands. All SDL
// failures are checked via backend::check() and fatal init failures
// pop an SDL message box (see backend_sdl3.cpp) so a broken video
// driver is visible instead of a silent exit.
//
// Init tracks ImGui progress in g_imgui_* flags: SDL_AppQuit runs even
// when init fails, so teardown shuts down only what was initialized.
// The frame body is exception-guarded: nothing thrown by poll/draw may
// cross the SDL C callback. Close requests while a job runs set
// quit_requested and quit once the job completes, so shutdown never
// hides the window behind a blocked join.

#include "app.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <memory>
#include <string>

namespace
{

using wemod::gui::AppState;
using wemod::gui::backend::check;
using wemod::gui::backend::pick_window_size;
using wemod::gui::backend::show_error;

// Init progress: SDL_AppQuit runs on every init failure path.
bool g_imgui_context{false};
bool g_imgui_sdl3{false};
bool g_imgui_renderer{false};

[[nodiscard]] AppState* state_of(void* appstate) noexcept
{
    return static_cast<AppState*>(appstate);
}

// Non-fatal per-frame check: [[nodiscard]] result is consumed by the
// (void) cast, failure still logs (and boxes when parented).
void check_frame(bool ok, const char* what) noexcept
{
    (void)check(ok, what, nullptr, false);
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

    // SDL_AppQuit runs even when init fails - it tears down only the
    // g_imgui_* stages reached here, so failure paths below destroy
    // just the window/renderer and return.
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
    check_frame(SDL_SetRenderVSync(renderer, 1), "SDL_SetRenderVSync");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    g_imgui_context = true;
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
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return SDL_APP_FAILURE;
    }
    g_imgui_sdl3 = true;
    if (!check(ImGui_ImplSDLRenderer3_Init(renderer),
               "ImGui_ImplSDLRenderer3_Init", nullptr, true)) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return SDL_APP_FAILURE;
    }
    g_imgui_renderer = true;

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
    if (event->type != SDL_EVENT_QUIT) {
        (void)appstate;
        return SDL_APP_CONTINUE;
    }
    AppState* state{state_of(appstate)};
    if (state == nullptr) {
        return SDL_APP_SUCCESS;
    }
    // A close request during a run does not quit immediately: the
    // popen() worker cannot be cancelled mid-child, so quitting now
    // would hide the window behind a blocked join. Stay open and let
    // SDL_AppIterate quit once the job completes.
    if (state->job.running() && !state->quit_requested) {
        state->quit_requested = true;
        state->job.request_stop();
        try {
            state->append_log("Finishing the background job before exit - "
                              "the window stays open until it completes.\n\n");
        } catch (...) {
        }
        state->scroll_to_bottom = true;
        return SDL_APP_CONTINUE;
    }
    return SDL_APP_SUCCESS;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    AppState* state{state_of(appstate)};
    if (state == nullptr || state->renderer == nullptr) {
        return SDL_APP_FAILURE;
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // Nothing here may cross the SDL C callback: allocation and
    // filesystem errors become a log line, the frame still renders.
    try {
        if (std::string picked;
            wemod::gui::backend::take_pending_folder(picked)) {
            state->install_dir = std::move(picked);
        }
        wemod::gui::core::poll(*state);
        wemod::gui::view::draw(*state);
    } catch (const std::exception& ex) {
        wemod::gui::backend::log_error(std::string("SDL_AppIterate: ") +
                                       ex.what());
    } catch (...) {
        wemod::gui::backend::log_error("SDL_AppIterate: unknown error");
    }

    ImGui::Render();
    // The only DPI knob: map imgui window coordinates onto the
    // HIGH_PIXEL_DENSITY framebuffer. The SDL_Renderer backend skips
    // its own clip-scale when a render scale is set, which also keeps
    // InputText hints from clipping.
    // SDL3 bool API returns true on success (no `== 0` comparison).
    const ImGuiIO& io{ImGui::GetIO()};
    check_frame(SDL_SetRenderScale(state->renderer,
                                   io.DisplayFramebufferScale.x,
                                   io.DisplayFramebufferScale.y),
                "SDL_SetRenderScale");
    const ImVec4& clear{wemod::gui::view::frame_clear_color()};
    check_frame(SDL_SetRenderDrawColorFloat(state->renderer, clear.x,
                                            clear.y, clear.z, clear.w),
                "SDL_SetRenderDrawColorFloat");
    check_frame(SDL_RenderClear(state->renderer), "SDL_RenderClear");
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),
                                          state->renderer);
    check_frame(SDL_RenderPresent(state->renderer), "SDL_RenderPresent");
    if (state->quit_requested && !state->job.running()) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    (void)result;
    const std::unique_ptr<AppState> owner{state_of(appstate)};

    // Init may have failed partway: shut down only reached stages.
    // Failure paths above already destroyed renderer/window while
    // *appstate stayed null, so no double teardown here.
    if (g_imgui_renderer) {
        ImGui_ImplSDLRenderer3_Shutdown();
        g_imgui_renderer = false;
    }
    if (g_imgui_sdl3) {
        ImGui_ImplSDL3_Shutdown();
        g_imgui_sdl3 = false;
    }
    if (g_imgui_context) {
        ImGui::DestroyContext();
        g_imgui_context = false;
    }

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
