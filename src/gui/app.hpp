#pragma once
// WeMod Enhancer - GUI contract (pure ISO C++23, std only).
//
// The ONLY channel between the SDL3 platform side and the Dear ImGui
// view side. Neither side includes the other API:
//   - sdl_platform.cpp includes <SDL3/SDL.h> + this header.
//   - imgui_view.cpp  includes <imgui.h>      + this header.
//   - main.cpp        wires both (composition root, may know both).
//
// No ImGui types, no SDL types, no UI styling, no OS process APIs
// here: window sizes live in sdl_platform.cpp, paddings/colors live
// in imgui_view.cpp, child-process capture lives in main.cpp.
// Communication is data, not calls:
//   view  reads app_state, writes frame_requests + outbox.
//   main  executes requests via platform:: services + background_runner.
//   jobs  run on a std::jthread worker, polled without blocking.

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <ranges>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef WEMOD_ENHANCER_GUI_VERSION
#define WEMOD_ENHANCER_GUI_VERSION "0.0.0"
#endif

namespace wemod::gui
{

namespace fs = std::filesystem;

constexpr std::string_view kGuiVersion{WEMOD_ENHANCER_GUI_VERSION};
constexpr std::string_view kPatcherName{"wemod_enhancer.py"};
constexpr std::string_view kVersionDllName{"version.dll"};
constexpr std::string_view kInstallerUrl{
    "https://api.wemod.com/client/download"};
constexpr std::string_view kLauncherCloneUrl{
    "https://github.com/DaniAsh551/wemod-launcher.git"};
constexpr std::string_view kLauncherGuideUrl{
    "https://deckcheatz.com/wemod-on-linux-full-guide/"};
constexpr std::string_view kIssueNewUrl{
    "https://github.com/e-gleba/wemod_enhancer/issues/new"};
constexpr std::string_view kReleasesUrl{
    "https://github.com/e-gleba/wemod_enhancer/releases/latest"};
constexpr std::string_view kQuickstartUrl{
    "https://github.com/e-gleba/wemod_enhancer#quick-start"};
constexpr std::string_view kReadmeUrl{
    "https://github.com/e-gleba/wemod_enhancer#wemod-enhancer"};
constexpr std::string_view kPythonUrl{"https://www.python.org/downloads/"};
constexpr std::string_view kIssuesUrl{
    "https://github.com/e-gleba/wemod_enhancer/issues/new"};

#ifdef _WIN32
constexpr bool kIsWindows{true};
#else
constexpr bool kIsWindows{false};
#endif

constexpr std::string_view kDefaultPython{
    kIsWindows ? std::string_view{"python"} : std::string_view{"python3"}};

constexpr std::size_t kLogBudget{512UZ * 1024UZ};
constexpr std::size_t kIssueLogBudget{3000};
constexpr auto kReprobeInterval{std::chrono::milliseconds(500)};

enum class run_kind : std::uint8_t { patcher, probe, wemod };
enum class probe_state : std::uint8_t { unknown, failed, works };

struct run_result final
{
    std::int32_t exit_code{-1};
    std::string output;
};

struct alert_request final
{
    std::string title;
    std::string message;
};

// Everything the view needs. No SDL / ImGui types, no UI styling.
struct app_state final
{
    std::string install_dir;
    std::string script_path;
    std::string python{std::string{kDefaultPython}};
    std::string version_dll;
    std::string log;
    bool running{false};
    run_kind kind{run_kind::patcher};
    bool scroll_to_bottom{false};
    bool has_run{false};
    std::int32_t last_exit_code{0};
    float copied_flash{0.0F};
    std::string probed_install_dir;
    std::string probed_script_path;
    std::string probed_version_dll;
    fs::path resolved_install_dir;
    bool script_present{false};
    bool dll_present{false};
    std::chrono::steady_clock::time_point last_probe{};
    probe_state python_ok{probe_state::unknown};
    std::string python_version;
    std::string platform_detail;
    std::string platform_name;
    std::string exe_dir_text;
    // outbox: view writes on the UI thread, platform drains on the UI
    // thread, except picked_folder which the SDL dialog callback writes
    // from its own thread (guarded by outbox_mutex).
    bool want_browse{false};
    std::optional<std::string> want_open_url;
    std::optional<std::string> want_clipboard;
    std::optional<alert_request> want_alert;
    std::mutex outbox_mutex;
    std::optional<std::string> picked_folder;
};

// One frame of view intents. Main executes them after draw.
struct frame_requests final
{
    bool patch{false};
    bool restore{false};
    bool download{false};
    bool copy{false};
    bool clear{false};
    bool report{false};
};

// --- pure logic (std only, ranges + format) ---------------------------

[[nodiscard]] constexpr std::string_view
running_status(const run_kind kind) noexcept
{
    switch (kind) {
    case run_kind::probe:
        return "Checking Python...";
    case run_kind::wemod:
        return kIsWindows ? std::string_view{"Downloading WeMod..."}
                          : std::string_view{"Cloning wemod-launcher..."};
    case run_kind::patcher:
        return "Running the patcher...";
    }
    return {};
}

[[nodiscard]] constexpr std::string_view
run_block_reason(const bool install_ok, const bool script_ok) noexcept
{
    if (!install_ok && !script_ok) {
        return "Needs a WeMod folder and the patcher script";
    }
    if (!install_ok) {
        return "Select a WeMod folder first";
    }
    if (!script_ok) {
        return "Patcher script missing - open Settings";
    }
    return {};
}

[[nodiscard]] inline std::string url_encode(const std::string_view text)
{
    constexpr std::string_view unreserved{"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                           "abcdefghijklmnopqrstuvwxyz"
                                           "0123456789-_.~"};
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text) {
        if (unreserved.find(static_cast<char>(c)) != std::string_view::npos) {
            out += static_cast<char>(c);
        } else {
            out += std::format("%{:02X}", c);
        }
    }
    return out;
}

[[nodiscard]] inline std::string shell_quote(const std::string_view arg)
{
    if constexpr (kIsWindows) {
        std::string out{"\""};
        for (const char c : arg) {
            if (c == '"') {
                out += "\"\"";
            } else {
                out += c;
            }
        }
        out += '"';
        return out;
    } else {
        std::string out{"'"};
        for (const char c : arg) {
            if (c == '\'') {
                out += "'\"'\"'";
            } else {
                out += c;
            }
        }
        out += '\'';
        return out;
    }
}

// "app-10.2.3" -> {10,2,3}; non-numeric tokens become 0.
[[nodiscard]] inline std::vector<std::int32_t>
version_parts(std::string name)
{
    constexpr std::string_view prefix{"app-"};
    if (name.starts_with(prefix)) {
        name.erase(0, prefix.size());
    }
    std::vector<std::int32_t> parts;
    std::size_t pos{0};
    while (pos < name.size()) {
        const std::size_t dot{name.find('.', pos)};
        const std::string_view token{name.data() + pos,
                                     (dot == std::string::npos ? name.size()
                                                              : dot) -
                                         pos};
        std::int32_t value{0};
        const char* const begin{token.data()};
        const char* const end{begin + token.size()};
        if (const auto res{std::from_chars(begin, end, value)};
            res.ec != std::errc{} || res.ptr != end) {
            value = 0;
        }
        parts.push_back(value);
        if (dot == std::string::npos) {
            break;
        }
        pos = dot + 1;
    }
    return parts;
}

[[nodiscard]] inline fs::path newest_app_dir(const fs::path& root)
{
    std::error_code ec;
    std::vector<fs::path> apps;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_directory(ec) && !ec &&
            entry.path().filename().string().starts_with("app-")) {
            apps.push_back(entry.path());
        }
    }
    const auto newest{std::ranges::max_element(
        apps, {}, [](const fs::path& p) {
            return version_parts(p.filename().string());
        })};
    return newest == apps.end() ? fs::path{} : *newest;
}

[[nodiscard]] inline fs::path
resolve_wemod_dir(const std::string& dir)
{
    if (dir.empty()) {
        return {};
    }
    std::error_code ec;
    const fs::path picked{dir};
    if (fs::is_regular_file(picked / "resources" / "app.asar", ec)) {
        return picked;
    }
    if (const fs::path app{newest_app_dir(picked)}; !app.empty()) {
        return app;
    }
    const fs::path launcher_bin{picked / "wemod_data" / "wemod_bin"};
    if (fs::is_regular_file(launcher_bin / "resources" / "app.asar", ec)) {
        return launcher_bin;
    }
    return {};
}

inline void append_log(app_state& state, const std::string_view text)
{
    state.log += text;
    if (state.log.size() > kLogBudget) {
        const std::size_t over{state.log.size() - kLogBudget};
        const std::size_t nl{state.log.find('\n', over)};
        state.log.erase(0, nl == std::string::npos ? over : nl + 1);
    }
}

[[nodiscard]] inline std::string
env_info(const app_state& state)
{
    std::string info{"--- environment ---\n"};
    info += std::format("gui version: {}\n", kGuiVersion);
    info += std::format("platform: {} {}\n", state.platform_name,
                        state.platform_detail);
    info += std::format("exe dir: {}\n", state.exe_dir_text);
    info += std::format("wemod folder: {}\n",
                        state.install_dir.empty() ? "<not set>"
                                                 : state.install_dir);
    info += std::format("patcher script: {}\n",
                        state.script_path.empty() ? "<missing next to exe>"
                                                 : state.script_path);
    info += std::format("python command: {}\n", state.python);
    info += std::format("python: {}\n",
                        state.python_version.empty() ? "<not probed>"
                                                    : state.python_version);
    if (state.has_run) {
        info += std::format("last exit code: {}\n", state.last_exit_code);
    }
    return info;
}

[[nodiscard]] inline std::string
issue_url(const app_state& state)
{
    std::string body{"## log\n\n```\n"};
    if (state.log.size() > kIssueLogBudget) {
        body += "... (log tail)\n";
        body += state.log.substr(state.log.size() - kIssueLogBudget);
    } else {
        body += state.log;
    }
    body += "```\n\n" + env_info(state);
    return std::string{kIssueNewUrl} + "?template=bug_report.yml&title=" +
        url_encode("bug: gui report") + "&body=" + url_encode(body);
}

inline void parse_probe(app_state& state, const std::string& output)
{
    const auto line = [&](const std::size_t from) {
        const std::size_t eol{output.find('\n', from)};
        std::string_view text{output.data() + from,
                              (eol == std::string::npos ? output.size()
                                                        : eol) -
                                  from};
        while (!text.empty() &&
               (text.back() == '\r' || text.back() == ' ')) {
            text.remove_suffix(1);
        }
        return std::string{text};
    };
    if (const std::string version{line(0)}; !version.empty()) {
        state.python_version = "Python " + version;
    }
    if (const std::size_t eol{output.find('\n')}; eol != std::string::npos) {
        state.platform_detail = line(eol + 1);
    }
}

// Killable capture, defined in main.cpp (composition root): polls the
// child and terminates it on stop_token, so request_stop() never hangs
// on a silent child. Declared here so the header stays std-only.
[[nodiscard]] run_result run_capture_impl(const std::string& command,
                                          const std::stop_token& token);

// --- background jobs on std::jthread (no detached threads) ------------
// Single worker: launch() is a no-op while busy; poll() is
// non-blocking and called once per frame from main.
class background_runner final
{
  public:
    background_runner() = default;
    background_runner(const background_runner&) = delete;
    background_runner& operator=(const background_runner&) = delete;
    ~background_runner() { request_stop(); }

    struct finished_job final
    {
        run_kind kind{run_kind::patcher};
        std::string shown;
        run_result result;
    };

    [[nodiscard]] bool busy() const noexcept
    {
        const std::lock_guard lock{mutex_};
        return busy_;
    }

    void launch(run_kind kind, std::string shown, std::string command)
    {
        {
            const std::lock_guard lock{mutex_};
            if (busy_) {
                return;
            }
            busy_ = true;
        }
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
        worker_ = std::jthread(
            [this, kind, shown = std::move(shown),
             command = std::move(command)](const std::stop_token token) {
                run_result result{run_capture_impl(command, token)};
                const std::lock_guard done_lock{mutex_};
                if (token.stop_requested()) {
                    busy_ = false;
                    return;
                }
                finished_.push(
                    finished_job{kind, std::move(shown), std::move(result)});
            });
    }

    [[nodiscard]] std::optional<finished_job> poll()
    {
        const std::lock_guard lock{mutex_};
        if (finished_.empty()) {
            return std::nullopt;
        }
        finished_job job{std::move(finished_.front())};
        finished_.pop();
        if (finished_.empty()) {
            busy_ = false;
        }
        return job;
    }

    void request_stop()
    {
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
        const std::lock_guard lock{mutex_};
        busy_ = false;
    }

  private:
    mutable std::mutex mutex_;
    std::jthread worker_;
    std::queue<finished_job> finished_;
    bool busy_{false};
};

// --- view + platform frontiers (defined in their own .cpp) ------------
namespace view
{
[[nodiscard]] frame_requests draw(app_state& state);
} // namespace view

namespace platform
{
// Opaque window/renderer handles: the header never names SDL types.
// dialog_default keeps the folder-dialog default path alive while the
// async SDL dialog is open (the field may be edited meanwhile).
struct context final
{
    void* window{nullptr};
    void* renderer{nullptr};
    std::string dialog_default;
};

[[nodiscard]] std::pair<std::int32_t, std::int32_t>
preferred_size() noexcept;
bool init(context& ctx, app_state& state);
void drain_outbox(context& ctx, app_state& state, float delta_seconds);
void begin_frame(context& ctx, float scale_x, float scale_y,
                 const std::array<float, 4>& clear);
void end_frame(context& ctx);
void shutdown(context& ctx);
void fatal(std::string_view title, std::string_view message) noexcept;
[[nodiscard]] std::string exe_dir();
[[nodiscard]] std::string default_install_dir();
[[nodiscard]] std::string platform_name();
void persist_log(const app_state& state);
} // namespace platform

} // namespace wemod::gui
