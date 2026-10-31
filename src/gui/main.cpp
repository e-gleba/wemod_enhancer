#include "app.hpp"
#include "cli.hpp"
#include "platform.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <gsl/assert>
#include <gsl/narrow>
#include <gsl/pointers>
#include <gsl/util>

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <system_error>

namespace wemod::gui
{
namespace
{
constexpr std::array<float, 4> clear_color = {0.10F, 0.10F, 0.12F, 1.00F};
constexpr auto process_poll_interval = std::chrono::milliseconds{10};

struct app final
{
    platform::context platform;
    app_state state;
    background_runner jobs;
    bool imgui_ready = false;
};

void start_command(app& value, const run_kind kind,
                   const std::string_view shown, std::vector<std::string> args)
{
    Expects(!shown.empty());
    Expects(!args.empty());
    if (value.jobs.busy() || !value.jobs.launch(kind, std::move(args))) {
        return;
    }
    append_log(value.state, std::format("$ {}\n", shown));
    value.state.kind = kind;
    value.state.running = true;
    value.state.has_run |= kind == run_kind::patcher;
}

void start_run(app& value, const std::string_view subcommand)
{
    Expects(subcommand == "patch" || subcommand == "restore");
    std::string shown = std::format("{} -u {} {} --install-dir {}",
                                     value.state.python, value.state.script_path,
                                     subcommand, value.state.install_dir);
    std::vector<std::string> args = {value.state.python, "-u",
                                     value.state.script_path,
                                     std::string{subcommand}, "--install-dir",
                                     value.state.install_dir};
    start_command(value, run_kind::patcher, shown, std::move(args));
}

void start_wemod_download(app& value)
{
    if constexpr (is_windows) {
        const char* downloads = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
        if (downloads == nullptr) {
            value.state.want_alert =
                alert_request{.title = "Cannot download", .message = SDL_GetError()};
            return;
        }
        const fs::path installer = fs::path{downloads} / "wemod_setup.exe";
        const std::string script = std::format(
            "$ProgressPreference='SilentlyContinue';Invoke-WebRequest "
            "-Uri '{}' -OutFile '{}';Start-Process '{}'",
            installer_url, installer.string(), installer.string());
        const std::string shown = std::format(
            "powershell -NoProfile -ExecutionPolicy Bypass "
            "-Command \"{}\"",
            script);
        start_command(value, run_kind::wemod, shown,
                      {"powershell", "-NoProfile", "-ExecutionPolicy",
                       "Bypass", "-Command", script});
    } else {
        const char* home =
            SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "HOME");
        if (home == nullptr) {
            value.state.want_alert =
                alert_request{.title = "Cannot download", .message = "HOME is not set."};
            return;
        }
        const fs::path directory = fs::path{home} / "wemod-launcher";
        std::error_code error;
        if (fs::is_directory(directory, error)) {
            value.state.install_dir = directory.string();
            append_log(value.state,
                       std::format("wemod-launcher already cloned: {}\n\n",
                                   directory.string()));
            value.state.scroll_to_bottom = true;
            return;
        }
        value.state.want_open_url = std::string{launcher_guide_url};
        const std::string shown = std::format("git clone {} {}",
                                              launcher_clone_url,
                                              directory.string());
        start_command(value, run_kind::wemod, shown,
                      {"git", "clone", std::string{launcher_clone_url},
                       directory.string()});
    }
}

void start_probe(app& value)
{
    constexpr std::string_view probe =
        "import sys,platform;print(sys.version.split()[0]);"
        "print(platform.platform())";
    const std::string shown =
        std::format("{} -c \"{}\"", value.state.python, probe);
    start_command(value, run_kind::probe, shown,
                  {value.state.python, "-c", std::string{probe}});
}

void poll_jobs(app& value)
{
    while (auto finished = value.jobs.poll()) {
        append_log(value.state, finished->result.output);
        if (!finished->result.output.empty() &&
            !finished->result.output.ends_with('\n')) {
            append_log(value.state, "\n");
        }
        append_log(value.state,
                   std::format("[exit code: {}]\n\n",
                               finished->result.exit_code));
        value.state.last_exit_code = finished->result.exit_code;
        value.state.scroll_to_bottom = true;

        if (finished->kind == run_kind::probe) {
            value.state.python_ok = finished->result.exit_code == 0
                ? probe_state::works
                : probe_state::failed;
            parse_probe(value.state, finished->result.output);
        } else if (finished->kind == run_kind::wemod &&
                   finished->result.exit_code == 0 && !is_windows) {
            value.state.install_dir = platform::default_install_dir();
            value.state.last_probe = {};
            append_log(value.state,
                       "wemod-launcher cloned. Run it once and log in.\n\n");
        } else if (finished->kind == run_kind::patcher &&
                   finished->result.exit_code != 0) {
            append_log(value.state,
                       "hint: close WeMod fully, then retry. If it still "
                       "fails, press Report bug.\n\n");
        }
    }
    value.state.running = value.jobs.busy();
}

void execute(app& value, const frame_requests& requests)
{
    if (requests.patch) {
        start_run(value, "patch");
    } else if (requests.restore) {
        start_run(value, "restore");
    } else if (requests.download) {
        start_wemod_download(value);
    }
    if (requests.copy) {
        value.state.want_clipboard =
            std::format("{}\n{}", value.state.log, env_info(value.state));
    }
    if (requests.clear) {
        value.state.log.clear();
        value.state.copied_flash = 0.0F;
    }
    if (requests.report) {
        // Clipboard first: the form field has a length cap and the URL can
        // hit the 414 limit on long logs, so pasting stays the fallback.
        value.state.want_clipboard = report_text(value.state);
        value.state.want_open_url = issue_url(value.state);
    }
}

void configure_imgui()
{
    // Stock ImGui theme on purpose: upstream keeps it coherent across
    // imgui updates, a hand-rolled palette drifts and fights the widgets.
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2{18.0F, 14.0F};
    style.FramePadding = ImVec2{12.0F, 8.0F};
    style.ItemSpacing = ImVec2{10.0F, 10.0F};
    style.ItemInnerSpacing = ImVec2{8.0F, 6.0F};
    style.ChildRounding = 8.0F;
    style.FrameRounding = 6.0F;
    style.GrabRounding = 6.0F;
    style.PopupRounding = 6.0F;
    style.ScrollbarRounding = 9.0F;
    style.TabRounding = 6.0F;
    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.ScrollbarSize = 14.0F;
    style.GrabMinSize = 14.0F;
}

[[nodiscard]] bool init_imgui(const platform::context& context) noexcept
{
    const platform::native_context handles = platform::native(context);
    Expects(handles.window != nullptr);
    Expects(handles.renderer != nullptr);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    configure_imgui();
    if (!ImGui_ImplSDL3_InitForSDLRenderer(handles.window, handles.renderer)) {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplSDLRenderer3_Init(handles.renderer)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    return true;
}

void shutdown_imgui() noexcept
{
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.BackendRendererUserData != nullptr) {
        ImGui_ImplSDLRenderer3_Shutdown();
    }
    if (io.BackendPlatformUserData != nullptr) {
        ImGui_ImplSDL3_Shutdown();
    }
    ImGui::DestroyContext();
}

// noexcept firewall by design: runs inside SDL's entry point, so
// allocation failure terminates here instead of escaping into SDL.
// NOLINTNEXTLINE(bugprone-exception-escape)
void report_cli(const cli::decision& early) noexcept
{
    // SDL_Log is the cross-platform console sink: stderr on POSIX, the
    // debugger elsewhere, and on Windows SDL attaches the parent console
    // itself — `gui.exe --help` prints in the invoking terminal with no
    // windows.h in our code. See SDL3 src/SDL_log.c.
    // One call per line: SDL appends the newline and embedded ones get
    // platform-specific handling. See SDL3 SDL_log.h docs.
    const bool error = early.kind == cli::mode::exit_err;
    for (const auto part : early.output | std::views::split('\n')) {
        const std::string line = {part.begin(), part.end()};
        if (line.empty()) {
            continue;
        }
        if (error) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", line.c_str());
        } else {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            SDL_Log("%s", line.c_str());
        }
    }
}

} // namespace

run_result run_capture(const std::vector<std::string>& args,
                       const std::stop_token& token)
{
    Expects(!args.empty());
    run_result result;
    // SDL3 process: argv stays argv on every platform, no shell anywhere.
    // SDL quotes each arg for CreateProcessW on Windows (UTF-8 paths and
    // spaces survive) and execs directly on POSIX, so cmd.exe /bin/sh
    // metacharacters are inert. See: https://wiki.libsdl.org/SDL3/CategoryProcess
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1UZ);
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    SDL_PropertiesID props = SDL_CreateProperties();
    if (props == 0) {
        result.output = std::format("error: SDL_CreateProperties failed ({})",
                                    SDL_GetError());
        return result;
    }
    // SDL copies what it needs at create time; argv only has to outlive
    // the SDL_CreateProcessWithProperties call below, not the process.
    const bool args_set = SDL_SetPointerProperty(
        props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
        // SDL stores args as void* and only reads them; argv outlives
        // the create call below, which is all SDL needs.
        // NOLINTNEXTLINE(bugprone-multi-level-implicit-pointer-conversion)
        static_cast<void*>(argv.data()));
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,
                          SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                          SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props,
                           SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN,
                           true);
    if (!args_set) {
        result.output = std::format("error: SDL_CreateProcess failed ({})",
                                    SDL_GetError());
        SDL_DestroyProperties(props);
        return result;
    }
    SDL_Process* process = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (process == nullptr) {
        result.output = std::format("error: SDL_CreateProcess failed ({})",
                                    SDL_GetError());
        return result;
    }

    SDL_IOStream* output = SDL_GetProcessOutput(process);
    if (output == nullptr) {
        result.output = std::format("error: SDL_GetProcessOutput failed ({})",
                                    SDL_GetError());
        SDL_KillProcess(process, true);
        SDL_DestroyProcess(process);
        return result;
    }

    std::array<char, 4096> buffer = {};
    bool killed = false;
    while (true) {
        const std::size_t count =
            SDL_ReadIO(output, buffer.data(), buffer.size());
        if (count > 0) {
            result.output.append(buffer.data(), count);
            continue;
        }
        if (SDL_GetIOStatus(output) == SDL_IO_STATUS_ERROR) {
            break;
        }
        int exit_code = 0;
        if (SDL_WaitProcess(process, false, &exit_code)) {
            break;
        }
        if (token.stop_requested() && !killed) {
            killed = true;
            SDL_KillProcess(process, false);
        }
        SDL_Delay(gsl::narrow<Uint32>(process_poll_interval.count()));
    }
    // Drain whatever arrived between the last poll and exit.
    while (true) {
        const std::size_t count =
            SDL_ReadIO(output, buffer.data(), buffer.size());
        if (count == 0) {
            break;
        }
        result.output.append(buffer.data(), count);
    }
    int exit_code = 0;
    SDL_WaitProcess(process, true, &exit_code);
    result.exit_code = exit_code;
    SDL_DestroyProcess(process);
    return result;
}
} // namespace wemod::gui

using wemod::gui::app;
using wemod::gui::cli::decision;
using wemod::gui::cli::mode;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
try {
    Expects(appstate != nullptr);
    // SDL owns argv's lifetime here; the span is a borrowed view only.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::span<char*> args{argv, static_cast<std::size_t>(argc)};
    // CLI first: --help/--version report through SDL_Log and exit
    // before any window exists. No second main; the GUI path below
    // is untouched.
    if (const decision early = wemod::gui::cli::decide(args);
        early.kind != mode::gui) {
        wemod::gui::report_cli(early);
        *appstate = nullptr;
        return early.kind == mode::exit_ok ? SDL_APP_SUCCESS
                                           : SDL_APP_FAILURE;
    }
    auto value = std::make_unique<app>();
    if (!wemod::gui::platform::init(value->platform, value->state) ||
        !wemod::gui::init_imgui(value->platform)) {
        return SDL_APP_FAILURE;
    }
    value->imgui_ready = true;

    const std::string directory = wemod::gui::platform::exe_dir();
    value->state.install_dir =
        wemod::gui::platform::default_install_dir();
    value->state.script_path =
        (wemod::gui::fs::path{directory} / wemod::gui::patcher_name).string();

    if (const wemod::gui::fs::path detected =
            wemod::gui::resolve_wemod_dir(value->state.install_dir);
        !detected.empty()) {
        value->state.install_dir = detected.string();
        wemod::gui::append_log(
            value->state,
            std::format("auto-detected WeMod install: {}\n\n",
                        value->state.install_dir));
    }
    wemod::gui::start_probe(*value);
    *appstate = value.release();
    return SDL_APP_CONTINUE;
} catch (const std::exception& error) {
    wemod::gui::platform::fatal("Initialization failed", error.what());
    return SDL_APP_FAILURE;
} catch (...) {
    wemod::gui::platform::fatal("Initialization failed", "Unknown error");
    return SDL_APP_FAILURE;
}

// NOLINTNEXTLINE(readability-identifier-naming)
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    Expects(event != nullptr);
    if (appstate != nullptr) {
        ImGui_ImplSDL3_ProcessEvent(event);
    }
    return event->type == SDL_EVENT_QUIT ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
try {
    gsl::not_null value{static_cast<app*>(appstate)};
    if (!value->imgui_ready) {
        return SDL_APP_FAILURE;
    }
    wemod::gui::poll_jobs(*value);
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    const wemod::gui::frame_requests requests =
        wemod::gui::view::draw(value->state);
    wemod::gui::execute(*value, requests);
    ImGui::Render();

    const ImGuiIO& io = ImGui::GetIO();
    wemod::gui::platform::drain_outbox(value->platform, value->state,
                                       io.DeltaTime);
    wemod::gui::platform::begin_frame(
        value->platform, io.DisplayFramebufferScale.x,
        io.DisplayFramebufferScale.y, wemod::gui::clear_color);
    const wemod::gui::platform::native_context handles =
        wemod::gui::platform::native(value->platform);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),
                                          handles.renderer);
    wemod::gui::platform::end_frame(value->platform);
    return SDL_APP_CONTINUE;
} catch (const std::exception& error) {
    wemod::gui::platform::fatal("Frame failed", error.what());
    return SDL_APP_FAILURE;
} catch (...) {
    wemod::gui::platform::fatal("Frame failed", "Unknown error");
    return SDL_APP_FAILURE;
}

// NOLINTNEXTLINE(readability-identifier-naming)
void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    (void)result;
    std::unique_ptr<app> value{static_cast<app*>(appstate)};
    if (!value) {
        return;
    }
    value->jobs.request_stop();
    wemod::gui::platform::persist_log(value->state);
    if (value->imgui_ready) {
        wemod::gui::shutdown_imgui();
    }
    wemod::gui::platform::shutdown(value->platform);
}
