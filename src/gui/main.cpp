#include "app.hpp"
#include "platform.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <gsl/assert>
#include <gsl/pointers>

#include <array>
#include <format>
#include <memory>
#include <string>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace wemod::gui
{
namespace
{
constexpr std::array<float, 4> clear_color{0.10F, 0.10F, 0.12F, 1.00F};
constexpr auto process_poll_interval{std::chrono::milliseconds{10}};

struct app final
{
    platform::context platform;
    app_state state;
    background_runner jobs;
    bool imgui_ready{false};
};

void start_command(app& value, const run_kind kind,
                   const std::string_view shown, std::string command)
{
    Expects(!shown.empty());
    Expects(!command.empty());
    if (value.jobs.busy()) {
        return;
    }
    if (!value.jobs.launch(kind, std::move(command))) {
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
    std::string shown{std::format("{} -u {} {} --install-dir {}",
                                  value.state.python, value.state.script_path,
                                  subcommand, value.state.install_dir)};
    std::string command{std::format(
        "{} -u {} {} --install-dir {}", shell_quote(value.state.python),
        shell_quote(value.state.script_path), subcommand,
        shell_quote(value.state.install_dir))};
    if (subcommand == "patch" && !value.state.version_dll.empty()) {
        shown += std::format(" --version-dll {}", value.state.version_dll);
        command += std::format(" --version-dll {}",
                               shell_quote(value.state.version_dll));
    }
    start_command(value, run_kind::patcher, shown, std::move(command));
}

void start_wemod_download(app& value)
{
    if constexpr (is_windows) {
        const char* downloads{SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)};
        if (downloads == nullptr) {
            value.state.want_alert =
                alert_request{"Cannot download", SDL_GetError()};
            return;
        }
        const fs::path installer{fs::path{downloads} / "wemod_setup.exe"};
        const std::string command{std::format(
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"$ProgressPreference='SilentlyContinue'; Invoke-WebRequest "
            "-Uri '{}' -OutFile '{}'; Start-Process '{}'\"",
            installer_url, installer.string(), installer.string())};
        start_command(value, run_kind::wemod, command, command);
    } else {
        const char* home{
            SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "HOME")};
        if (home == nullptr) {
            value.state.want_alert =
                alert_request{"Cannot download", "HOME is not set."};
            return;
        }
        const fs::path directory{fs::path{home} / "wemod-launcher"};
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
        const std::string command{std::format("git clone {} {}",
                                              launcher_clone_url,
                                              shell_quote(directory.string()))};
        start_command(value, run_kind::wemod, command, command);
    }
}

void start_probe(app& value)
{
    constexpr std::string_view probe{
        "import sys,platform;print(sys.version.split()[0]);"
        "print(platform.platform())"};
    const std::string shown{
        std::format("{} -c \"{}\"", value.state.python, probe)};
    start_command(value, run_kind::probe, shown,
                  std::format("{} -c \"{}\"",
                              shell_quote(value.state.python), probe));
}

void poll_jobs(app& value)
{
    while (auto finished{value.jobs.poll()}) {
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
        value.state.want_open_url = issue_url(value.state);
    }
}

void configure_imgui()
{
    ImGuiStyle& style{ImGui::GetStyle()};
    style.WindowPadding = ImVec2{16.0F, 14.0F};
    style.FramePadding = ImVec2{14.0F, 8.0F};
    style.ItemSpacing = ImVec2{10.0F, 8.0F};
    style.ItemInnerSpacing = ImVec2{8.0F, 6.0F};
    style.ScrollbarSize = 16.0F;
    style.GrabMinSize = 14.0F;
}

[[nodiscard]] bool init_imgui(const platform::context& context) noexcept
{
    const platform::native_context handles{platform::native(context)};
    Expects(handles.window != nullptr);
    Expects(handles.renderer != nullptr);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
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
    const ImGuiIO& io{ImGui::GetIO()};
    if (io.BackendRendererUserData != nullptr) {
        ImGui_ImplSDLRenderer3_Shutdown();
    }
    if (io.BackendPlatformUserData != nullptr) {
        ImGui_ImplSDL3_Shutdown();
    }
    ImGui::DestroyContext();
}
}

run_result run_capture(const std::string_view command,
                       const std::stop_token token)
{
    Expects(!command.empty());
    run_result result;
#ifdef _WIN32
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE read_handle{nullptr};
    HANDLE write_handle{nullptr};
    if (!CreatePipe(&read_handle, &write_handle, &attributes, 0)) {
        result.output = "error: CreatePipe failed";
        return result;
    }
    SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_handle;
    startup.hStdError = write_handle;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    std::string command_line{"cmd.exe /d /s /c "};
    command_line += command;
    if (!CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr,
                        nullptr, &startup, &process)) {
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        result.output = "error: CreateProcess failed";
        return result;
    }
    CloseHandle(write_handle);

    std::array<char, 4096> buffer{};
    while (true) {
        if (token.stop_requested()) {
            TerminateProcess(process.hProcess, 1);
        }
        DWORD available{0};
        if (!PeekNamedPipe(read_handle, nullptr, 0, nullptr, &available,
                           nullptr)) {
            break;
        }
        while (available != 0) {
            DWORD read{0};
            const DWORD requested{std::min<DWORD>(
                available, gsl::narrow<DWORD>(buffer.size()))};
            if (!ReadFile(read_handle, buffer.data(), requested, &read,
                          nullptr) ||
                read == 0) {
                available = 0;
                break;
            }
            result.output.append(buffer.data(), read);
            available -= read;
        }
        const DWORD status{
            WaitForSingleObject(process.hProcess,
                                gsl::narrow<DWORD>(process_poll_interval.count()))};
        if (status == WAIT_OBJECT_0) {
            break;
        }
    }
    DWORD exit_code{1};
    GetExitCodeProcess(process.hProcess, &exit_code);
    result.exit_code = gsl::narrow<std::int32_t>(exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(read_handle);
#else
    int output[2]{-1, -1};
    if (::pipe2(output, O_CLOEXEC | O_NONBLOCK) != 0) {
        result.output = std::format("error: pipe2 failed ({})", errno);
        return result;
    }
    const pid_t pid{::fork()};
    if (pid < 0) {
        ::close(output[0]);
        ::close(output[1]);
        result.output = std::format("error: fork failed ({})", errno);
        return result;
    }
    if (pid == 0) {
        ::dup2(output[1], STDOUT_FILENO);
        ::dup2(output[1], STDERR_FILENO);
        ::close(output[0]);
        ::close(output[1]);
        const std::string command_text{command};
        ::execl("/bin/sh", "sh", "-c", command_text.c_str(), nullptr);
        ::_exit(127);
    }

    ::close(output[1]);
    std::array<char, 4096> buffer{};
    int status{0};
    while (true) {
        const ssize_t count{::read(output[0], buffer.data(), buffer.size())};
        if (count > 0) {
            result.output.append(buffer.data(), gsl::narrow<std::size_t>(count));
        }
        if (token.stop_requested()) {
            ::kill(pid, SIGKILL);
        }
        const pid_t waited{::waitpid(pid, &status, WNOHANG)};
        if (waited == pid) {
            break;
        }
        if (waited < 0 && errno != EINTR) {
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            break;
        }
        std::this_thread::sleep_for(process_poll_interval);
    }
    while (true) {
        const ssize_t count{::read(output[0], buffer.data(), buffer.size())};
        if (count <= 0) {
            break;
        }
        result.output.append(buffer.data(), gsl::narrow<std::size_t>(count));
    }
    ::close(output[0]);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    return result;
}

} // namespace wemod::gui

using wemod::gui::app;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
try {
    (void)argc;
    (void)argv;
    Expects(appstate != nullptr);
    auto value{std::make_unique<app>()};
    if (!wemod::gui::platform::init(value->platform, value->state) ||
        !wemod::gui::init_imgui(value->platform)) {
        return SDL_APP_FAILURE;
    }
    value->imgui_ready = true;

    const std::string directory{wemod::gui::platform::exe_dir()};
    value->state.install_dir =
        wemod::gui::platform::default_install_dir();
    value->state.script_path =
        (wemod::gui::fs::path{directory} / wemod::gui::patcher_name).string();
    value->state.version_dll =
        (wemod::gui::fs::path{directory} / wemod::gui::version_dll_name)
            .string();

    if (const wemod::gui::fs::path detected{
            wemod::gui::resolve_wemod_dir(value->state.install_dir)};
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

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) noexcept
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
    const wemod::gui::frame_requests requests{
        wemod::gui::view::draw(value->state)};
    wemod::gui::execute(*value, requests);
    ImGui::Render();

    const ImGuiIO& io{ImGui::GetIO()};
    wemod::gui::platform::drain_outbox(value->platform, value->state,
                                       io.DeltaTime);
    wemod::gui::platform::begin_frame(
        value->platform, io.DisplayFramebufferScale.x,
        io.DisplayFramebufferScale.y, wemod::gui::clear_color);
    const wemod::gui::platform::native_context handles{
        wemod::gui::platform::native(value->platform)};
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

void SDL_AppQuit(void* appstate, SDL_AppResult result) noexcept
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
