// WeMod Enhancer - desktop app (Dear ImGui + SDL3, C++23).
//
// A click-button wrapper around the Python patcher
// (scripts/wemod_enhancer.py): it picks the WeMod folder, runs it and
// shows stdout/stderr live, plus the exit code, in a scrolling log.
//
// The patcher ships inside the package: wemod_enhancer.py and
// version.dll sit next to the executable. No download, no update
// check, no network - the install is one self-contained movable
// folder. Required: Python 3.11+ on PATH and the WeMod folder
// (auto-detected at startup, so usually just press Patch).
//
// Layout (default imgui theme, untouched colors - hierarchy comes
// from alignment, padding and scale): see ui.cpp draw_ui().
//
// File map:
//   config.hpp   - shared constants, one preprocessor site.
//   runner.hpp   - background_runner: one jthread, UI owns the result.
//   platform.hpp - SDL3 OS layer: processes, dialogs, clipboard, window.
//   state.*      - app decisions: resolve dirs, launch jobs, drain log.
//   ui.*         - Dear ImGui widgets only, same strings as before.
//   app.cpp      - SDL3 callbacks: init, frame, quit (this file).
//
// Error policy: every SDL call is checked. Fatal startup failures raise
// the native assert window (SDL_ShowSimpleMessageBox) and log; frame
// hiccups log and continue; child failures narrate in the app log with
// a fix hint. No silent errno, no popen, no shell anywhere.
//
// NOTE: imgui's default font covers ASCII only - keep every literal in
// this file plain ASCII.
#include "config.hpp"
#include "platform.hpp"
#include "state.hpp"
#include "ui.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <gsl/assert>

#include <filesystem>
#include <memory>

namespace wemod::gui
{

namespace fs = std::filesystem;

// Theme clear color for the renderer (ui owns the widget palette).
constexpr float clear_r{0.10F};
constexpr float clear_g{0.10F};
constexpr float clear_b{0.12F};
constexpr float clear_a{1.00F};

// SDL_App* passes this back verbatim; app.cpp owns it via unique_ptr.
struct app final
{
    window win;
    std::unique_ptr<app_state> state;
};

} // namespace wemod::gui

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    using namespace wemod::gui;
    Expects(appstate != nullptr);
    (void)argc;
    (void)argv;

    // SDL_AppQuit runs even when SDL_AppInit fails and calls SDL_Quit
    // there - no cleanup needed in this scope.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fatal_message("WeMod Enhancer",
                      std::string{"SDL_Init: "} + SDL_GetError());
        return SDL_APP_FAILURE;
    }

    window win{make_window()};
    if (!win) {
        return SDL_APP_FAILURE; // make_window already boxed the reason
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Density only: default dark colors stay. Padding stays 1x in window
    // coordinates; SDL_SetRenderScale (DisplayFramebufferScale) maps those
    // onto the HiDPI framebuffer every frame.
    ImGuiStyle& style{ImGui::GetStyle()};
    style.WindowPadding = ImVec2(16.0F, 14.0F);
    style.FramePadding = ImVec2(14.0F, 8.0F);
    style.ItemSpacing = ImVec2(10.0F, 8.0F);
    style.ItemInnerSpacing = ImVec2(8.0F, 6.0F);
    style.ScrollbarSize = 16.0F;
    style.GrabMinSize = 14.0F;

    if (!ImGui_ImplSDL3_InitForSDLRenderer(win.handle, win.renderer)) {
        fatal_message("WeMod Enhancer",
                      std::string{"ImGui_ImplSDL3_InitForSDLRenderer: "} +
                          SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!ImGui_ImplSDLRenderer3_Init(win.renderer)) {
        fatal_message("WeMod Enhancer",
                      std::string{"ImGui_ImplSDLRenderer3_Init: "} +
                          SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto holder{std::make_unique<app>()};
    holder->win = std::move(win);
    holder->state = std::make_unique<app_state>();
    app_state& state{*holder->state};
    state.window_handle = holder->win.handle;
    state.install_dir = default_install_dir();
    state.script_path = (exe_dir() / patcher_script_name).string();
    state.version_dll = (exe_dir() / version_dll_name).string();
    state.python = std::string(default_python);

    // Say what was auto-detected up front - the log doubles as the
    // diagnostics report.
    if (const fs::path detected{resolve_wemod_dir(state.install_dir)};
        !detected.empty()) {
        state.install_dir = detected.string();
        append_log(state,
                   "auto-detected WeMod install: " + state.install_dir +
                       "\n\n");
    }

    // The patcher ships next to the exe: state the fact, good or bad,
    // then probe Python - nothing else runs before Patch.
    if (fs::is_regular_file(state.script_path)) {
        append_log(state,
                   "using bundled patcher: " + state.script_path + "\n\n");
    } else {
        append_log(state,
                   "error: wemod_enhancer.py is missing next to the "
                   "exe:\n  " +
                       state.script_path +
                       "\n  fix: re-download the GUI package from the "
                       "GitHub releases and unpack the whole folder - it "
                       "is self-contained.\n\n");
    }
    start_probe(state);

    *appstate = holder.release();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    Expects(event != nullptr);
    (void)appstate;
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    using namespace wemod::gui;
    Expects(appstate != nullptr);
    auto* holder{static_cast<app*>(appstate)};
    app_state& state{*holder->state};

    poll_run(state);

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    draw_ui(state);

    ImGui::Render();
    const ImGuiIO& io{ImGui::GetIO()};
    SDL_Renderer* ren{holder->win.renderer};
    if (!begin_frame(ren, io.DisplayFramebufferScale.x,
                     io.DisplayFramebufferScale.y, clear_r, clear_g, clear_b,
                     clear_a)) {
        return SDL_APP_CONTINUE; // logged inside; skip this frame
    }
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
    present_frame(ren);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    (void)result;
    // Re-acquire ownership: holder destroys state, then window.
    const std::unique_ptr<wemod::gui::app> holder{
        static_cast<wemod::gui::app*>(appstate)};

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_Quit();
}
