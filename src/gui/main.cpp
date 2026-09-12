// WeMod Enhancer - composition root (the ONLY file that knows both
// SDL3 and Dear ImGui). Wires platform:: + view:: through app.hpp:
// one frame = poll background jobs, draw view, execute frame_requests,
// drain SDL outbox, present.

#include "app.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace wemod::gui
{

// Killable capture: the ONLY popen/pclose site. A reader thread feeds
// the pipe into a mutex-guarded string while the owner polls
// waitpid(WNOHANG); stop_token kills the child, so request_stop()
// never blocks on a silent process. Defined here (not app.hpp) so the
// contract header stays pure ISO C++23 with no OS process APIs.
run_result run_capture_impl(const std::string& command,
                            const std::stop_token& token)
{
    run_result result;
#ifdef _WIN32
    // Windows: _popen has no child handle to kill. Poll the token
    // between reads; a silent child still blocks shutdown there.
    // Keep commands short-lived (patcher / probe / installer kick).
    FILE* pipe{_popen(command.c_str(), "r")};
    if (pipe == nullptr) {
        result.output = std::format(
            "error: failed to start the command ({})",
            std::system_category().message(errno));
        return result;
    }
    std::array<char, 4096> buffer{};
    while (!token.stop_requested() &&
           fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
               nullptr) {
        result.output += buffer.data();
    }
    result.exit_code = _pclose(pipe);
    return result;
#else
    const std::string full{command + " 2>&1"};
    int out_pipe[2]{-1, -1};
    if (::pipe(out_pipe) != 0) {
        result.output = std::format(
            "error: failed to start the command ({})",
            std::system_category().message(errno));
        return result;
    }
    const pid_t pid{fork()};
    if (pid < 0) {
        const int err{errno};
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        result.output = std::format(
            "error: failed to start the command ({})",
            std::system_category().message(err));
        return result;
    }
    if (pid == 0) {
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(out_pipe[1], STDERR_FILENO);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        execl("/bin/sh", "sh", "-c", full.c_str(), nullptr);
        _exit(127);
    }
    ::close(out_pipe[1]);
    FILE* stream{fdopen(out_pipe[0], "r")};
    if (stream == nullptr) {
        ::close(out_pipe[0]);
        ::kill(pid, SIGKILL);
        int status{0};
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        result.exit_code = -1;
        return result;
    }
    std::string captured;
    std::mutex capture_mutex;
    bool reader_done{false};
    std::thread reader{[stream, &captured, &capture_mutex, &reader_done] {
        std::array<char, 4096> buffer{};
        while (fgets(buffer.data(), static_cast<int>(buffer.size()),
                     stream) != nullptr) {
            const std::lock_guard lock{capture_mutex};
            captured += buffer.data();
        }
        const std::lock_guard lock{capture_mutex};
        reader_done = true;
    }};
    int status{0};
    int exit_code{-1};
    while (true) {
        if (token.stop_requested()) {
            ::kill(pid, SIGKILL);
        }
        const pid_t waited{::waitpid(pid, &status, WNOHANG)};
        if (waited == pid) {
            exit_code =
                WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            break;
        }
        if (waited < 0 && errno != EINTR) {
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Closing the stream unblocks the reader; join always terminates.
    ::fclose(stream);
    reader.join();
    const std::lock_guard lock{capture_mutex};
    result.output = std::move(captured);
    result.exit_code = exit_code;
    (void)reader_done;
    return result;
#endif
}

namespace
{

constexpr std::array<float, 4> kClearColor{0.10F, 0.10F, 0.12F, 1.00F};

struct app final
{
    platform::context platform;
    app_state state;
    background_runner jobs;
    bool imgui_ready{false};
};

void start_command(app& app, const run_kind kind, const std::string& shown,
                   const std::string& command)
{
    if (command.empty() || shown.empty() || app.jobs.busy()) {
        return;
    }
    append_log(app.state, "$ " + shown + "\n");
    app.state.kind = kind;
    app.state.running = true;
    if (kind == run_kind::patcher) {
        app.state.has_run = true;
    }
    app.jobs.launch(kind, shown, command);
}

void start_run(app& app, const std::string_view subcommand)
{
    if (subcommand != "patch" && subcommand != "restore") {
        return;
    }
    const std::string sub{subcommand};
    std::string shown{std::format("{} -u {} {} --install-dir {}",
                                  app.state.python, app.state.script_path, sub,
                                  app.state.install_dir)};
    std::string command{std::format("{} -u {} {} --install-dir {}",
                                    shell_quote(app.state.python),
                                    shell_quote(app.state.script_path), sub,
                                    shell_quote(app.state.install_dir))};
    if (subcommand == "patch" && !app.state.version_dll.empty()) {
        shown += std::format(" --version-dll {}", app.state.version_dll);
        command +=
            std::format(" --version-dll {}", shell_quote(app.state.version_dll));
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
        const std::string command{std::format(
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"$ProgressPreference='SilentlyContinue'; Invoke-WebRequest "
            "-Uri '{}' -OutFile '{}'; Start-Process '{}'\"",
            kInstallerUrl, installer.string(), installer.string())};
        start_command(app, run_kind::wemod, command, command);
    } else {
        const char* home{
            SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "HOME")};
        if (home == nullptr) {
            append_log(app.state, "error: HOME is not set - cannot clone "
                                  "wemod-launcher.\n\n");
            app.state.want_alert =
                alert_request{"Cannot download",
                              "HOME is not set - cannot clone wemod-launcher."};
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
        const std::string command{std::format("git clone {} {}",
                                              kLauncherCloneUrl,
                                              shell_quote(dir.string()))};
        start_command(app, run_kind::wemod, command, command);
    }
}

void start_probe(app& app)
{
    constexpr std::string_view probe{
        "import sys,platform;print(sys.version.split()[0]);"
        "print(platform.platform())"};
    const std::string shown{
        std::format("{} -c \"{}\"", app.state.python, probe)};
    start_command(app, run_kind::probe, shown,
                  std::format("{} -c \"{}\"", shell_quote(app.state.python),
                              probe));
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

void teardown_imgui() noexcept
{
    // SDL still calls SDL_AppQuit after SDL_AppInit fails, before any
    // context exists: guard every backend or ImGui asserts on null
    // BackendPlatformUserData / BackendRendererUserData.
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    const ImGuiIO& io{ImGui::GetIO()};
    if (io.BackendRendererUserData != nullptr) {
        ImGui_ImplSDLRenderer3_Shutdown();
    }
    if (io.BackendPlatformUserData != nullptr) {
        ImGui_ImplSDL3_Shutdown();
    }
    ImGui::DestroyContext();
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
    auto* window = static_cast<SDL_Window*>(boxed->platform.window);
    auto* renderer = static_cast<SDL_Renderer*>(boxed->platform.renderer);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    wemod::gui::style_once();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    boxed->imgui_ready = true;

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
    if (boxed == nullptr || !boxed->imgui_ready) {
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
                                      io.DisplayFramebufferScale.y, kClearColor);
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
    if (boxed && boxed->imgui_ready) {
        teardown_imgui();
    }
    if (boxed) {
        wemod::gui::platform::persist_log(boxed->state);
        wemod::gui::platform::shutdown(boxed->platform);
    } else {
        wemod::gui::platform::context empty;
        wemod::gui::platform::shutdown(empty);
    }
}
