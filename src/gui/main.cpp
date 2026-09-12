// WeMod Enhancer - composition root (the ONLY file that knows both
// SDL3 and Dear ImGui). Wires platform:: + view:: through app.hpp:
// one frame = poll background jobs, draw view, execute frame_requests,
// drain SDL outbox, present.
//
// C++23: std::format, std::ranges not needed here, std::jthread worker
// lives in background_runner (app.hpp). No detached threads.

#include "app.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <memory>
#include <string>

namespace wemod::gui
{

namespace
{

struct app final
{
    platform::context platform;
    app_state state;
    background_runner jobs;
};

void start_command(app& app, const run_kind kind, const std::string& shown,
                   const std::string& command)
{
    if (command.empty() || app.jobs.busy()) {
        return;
    }
    append_log(app.state, "$ " + shown + "\n");
    app.state.kind = kind;
    app.state.running = true;
    if (kind == run_kind::patcher) {
        app.state.has_run = true;
    }
    // Capture `command` by value: the worker owns its string.
    app.jobs.launch(kind, shown, command, [](const run_result&) {});
}

void start_run(app& app, const char* subcommand)
{
    const std::string sub{subcommand};
    const std::string shown{app.state.python + " -u " + app.state.script_path +
                            " " + sub + " --install-dir " +
                            app.state.install_dir};
    std::string command{shell_quote(app.state.python) + " -u " +
                        shell_quote(app.state.script_path) + " " + sub +
                        " --install-dir " +
                        shell_quote(app.state.install_dir)};
    if (sub == "patch" && !app.state.version_dll.empty()) {
        shown + " --version-dll " + app.state.version_dll;
        command += " --version-dll " + shell_quote(app.state.version_dll);
    }
    start_command(app, run_kind::patcher, shown, command);
}

void start_wemod_download(app& app)
{
    if constexpr (kIsWindows) {
        const char* downloads{SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)};
        if (downloads == nullptr) {
            SDL_Log("SDL_GetUserFolder: %s", SDL_GetError());
            return;
        }
        const fs::path installer{fs::path(downloads) / "wemod_setup.exe"};
        const std::string command{
            "powershell -NoProfile -ExecutionPolicy Bypass -Command \""\
            "$ProgressPreference='SilentlyContinue'; "\
            "Invoke-WebRequest -Uri '" +
            std::string{kInstallerUrl} + "' -OutFile '" +
            installer.string() + "'; Start-Process '" +
            installer.string() + "'\""};
        start_command(app, run_kind::wemod, command, command);
    } else {
        const char* home{
            SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "HOME")};
        if (home == nullptr) {
            append_log(app.state, "error: HOME is not set - cannot clone "
                                  "wemod-launcher.\n\n");
            return;
        }
        const fs::path dir{fs::path(home) / "wemod-launcher"};
        std::error_code ec;
        if (fs::is_directory(dir, ec)) {
            app.state.install_dir = dir.string();
            append_log(app.state,
                       "wemod-launcher already cloned: " + dir.string() +
                           "\n  run it once and log in - wemod_data/wemod_bin "
                           "appears after login, this field resolves to "
                           "it.\n\n");
            app.state.scroll_to_bottom = true;
            return;
        }
        app.state.want_open_url = std::string{kLauncherGuideUrl};
        const std::string command{"git clone " +
                                  std::string{kLauncherCloneUrl} + " " +
                                  shell_quote(dir.string())};
        start_command(app, run_kind::wemod, command, command);
    }
}

void start_probe(app& app)
{
    const std::string shown{app.state.python +
                            " -c \"import sys,platform;print(sys.version." \
                            "split()[0]);print(platform.platform())\""};
    start_command(app, run_kind::probe, shown,
                  shell_quote(app.state.python) +
                      " -c \"import sys,platform;print(sys.version.split()[0" \
                      "]);print(platform.platform())\"");
}

void poll_jobs(app& app)
{
    while (auto finished{app.jobs.poll()}) {
        const run_result& result{finished->result};
        append_log(app.state, result.output);
        if (!result.output.empty() && !result.output.ends_with('\n')) {
            append_log(app.state, "\n");
        }
        append_log(app.state,
                   std::format("[exit code: {}]\n\n", result.exit_code));
        app.state.last_exit_code = result.exit_code;
        app.state.running = app.jobs.busy();
        app.state.scroll_to_bottom = true;
        switch (finished->kind) {
        case run_kind::wemod:
            if (result.exit_code != 0) {
                append_log(app.state,
                           kIsWindows
                               ? "error: could not download the WeMod "
                                 "installer.\n  fix: check the network "
                                 "connection, then retry - or grab it from "
                                 "https://www.wemod.com/download\n\n"
                               : "error: could not clone wemod-launcher.\n"
                                 "  fix: check the network connection and "
                                 "that git is installed, then retry.\n\n");
                break;
            }
            if constexpr (kIsWindows) {
                append_log(app.state,
                           "WeMod installer downloaded and started.\n"
                           "  next: install, run WeMod once, log in - then "
                           "this app auto-detects the folder.\n\n");
            } else {
                if (const char* home{SDL_GetEnvironmentVariable(
                        SDL_GetEnvironment(), "HOME")}) {
                    app.state.install_dir =
                        (fs::path(home) / "wemod-launcher").string();
                }
                append_log(app.state,
                           "wemod-launcher cloned (tutorial opened in your "
                           "browser).\n"
                           "  next: run it once and log in - "
                           "wemod_data/wemod_bin appears after login, the "
                           "folder field resolves to it.\n\n");
            }
            break;
        case run_kind::probe:
            app.state.python_ok = result.exit_code == 0 ? probe_state::works
                                                        : probe_state::failed;
            parse_probe(app.state, result.output);
            break;
        case run_kind::patcher:
            if (result.exit_code != 0) {
                append_log(app.state,
                           "hint: close WeMod fully, then retry. If it still "
                           "fails, press Report bug below - the issue opens "
                           "pre-filled with this log.\n\n");
            }
            break;
        }
    }
    app.state.running = app.jobs.busy();
}

void execute(app& app, const frame_requests& req)
{
    if (req.patch) {
        start_run(app, "patch");
    } else if (req.restore) {
        start_run(app, "restore");
    }
    if (req.download) {
        start_wemod_download(app);
    }
    if (req.copy) {
        app.state.want_clipboard = app.state.log + "\n" + env_info(app.state);
    }
    if (req.clear) {
        app.state.log.clear();
        app.state.copied_flash = 0.0F;
    }
    if (req.report) {
        app.state.want_open_url = issue_url(app.state);
    }
}

void style_once()
{
    ImGuiStyle& style{ImGui::GetStyle()};
    style.WindowPadding = ImVec2(16.0F, 14.0F);
    style.FramePadding = ImVec2(14.0F, 8.0F);
    style.ItemSpacing = ImVec2(10.0F, 8.0F);
    style.ItemInnerSpacing = ImVec2(8.0F, 6.0F);
    style.ScrollbarSize = 16.0F;
    style.GrabMinSize = 14.0F;
}

} // namespace

} // namespace wemod::gui

using wemod::gui::app;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    if (appstate == nullptr) {
        return SDL_APP_FAILURE;
    }
    auto boxed{std::make_unique<app>()};
    if (!wemod::gui::platform::init(boxed->platform, boxed->state)) {
        return SDL_APP_FAILURE;
    }
    auto* window =
        static_cast<SDL_Window*>(boxed->platform.window);
    auto* renderer =
        static_cast<SDL_Renderer*>(boxed->platform.renderer);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    wemod::gui::style_once();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    const std::string exe{wemod::gui::platform::exe_dir()};
    boxed->state.install_dir = wemod::gui::platform::default_install_dir();
    boxed->state.script_path =
        (wemod::gui::fs::path(exe) / wemod::gui::kPatcherName).string();
    boxed->state.version_dll =
        (wemod::gui::fs::path(exe) / wemod::gui::kVersionDllName).string();
    boxed->state.python = std::string{wemod::gui::kDefaultPython};

    if (const auto detected{wemod::gui::resolve_wemod_dir(
                boxed->state.install_dir)};
        !detected.empty()) {
        boxed->state.install_dir = detected.string();
        wemod::gui::append_log(
            boxed->state,
            "auto-detected WeMod install: " + boxed->state.install_dir +
                "\n\n");
    }
    if (std::error_code ec;
        wemod::gui::fs::is_regular_file(boxed->state.script_path, ec)) {
        wemod::gui::append_log(
            boxed->state,
            "using bundled patcher: " + boxed->state.script_path + "\n\n");
    } else {
        wemod::gui::append_log(
            boxed->state,
            "error: wemod_enhancer.py is missing next to the exe:\n  " +
                boxed->state.script_path +
                "\n  fix: re-download the GUI package from the GitHub "
                "releases and unpack the whole folder - it is "
                "self-contained.\n\n");
    }
    start_probe(*boxed);
    *appstate = boxed.release();
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
    auto* boxed{static_cast<app*>(appstate)};
    if (boxed == nullptr) {
        return SDL_APP_FAILURE;
    }
    poll_jobs(*boxed);

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const wemod::gui::frame_requests req{
        wemod::gui::view::draw(boxed->state)};
    execute(*boxed, req);
    ImGui::Render();

    // Single DPI knob: map imgui window coords onto the
    // HIGH_PIXEL_DENSITY framebuffer. Never also bake FontScaleDpi /
    // ScaleAllSizes to the display scale (double-counts, ~2x too big).
    const ImGuiIO& io{ImGui::GetIO()};
    wemod::gui::platform::drain_outbox(boxed->platform, boxed->state,
                                       io.DeltaTime);
    wemod::gui::platform::begin_frame(boxed->platform,
                                      io.DisplayFramebufferScale.x,
                                      io.DisplayFramebufferScale.y,
                                      wemod::gui::kClearColor);
    ImGui_ImplSDLRenderer3_RenderDrawData(
        ImGui::GetDrawData(),
        static_cast<SDL_Renderer*>(boxed->platform.renderer));
    wemod::gui::platform::end_frame(boxed->platform);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    (void)result;
    const std::unique_ptr<app> boxed{static_cast<app*>(appstate)};
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (boxed) {
        wemod::gui::platform::persist_log(boxed->state);
        // Order: destroy SDL objects before SDL_Quit (inside shutdown).
        wemod::gui::platform::shutdown(
            const_cast<wemod::gui::platform::context&>(boxed->platform));
    } else {
        wemod::gui::platform::context empty;
        wemod::gui::platform::shutdown(empty);
    }
}
