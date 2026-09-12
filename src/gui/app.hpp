// WeMod Enhancer GUI - single public header.
//
// Layering (one header, four translation units, no cycles):
//   backend_sdl3.cpp - SDL3 only. Init helpers, message boxes,
//                      clipboard, URLs, folder dialogs, environment,
//                      paths, display sizing. No ImGui.
//   core.cpp         - pure domain logic. Filesystem probing, WeMod dir
//                      resolution, shell quoting, process capture,
//                      background jobs (std::jthread). No SDL headers,
//                      no ImGui headers - SDL via backend:: below.
//   view_imgui.cpp   - Dear ImGui only. All widgets and layout. SDL
//                      side effects via backend::, facts via core::.
//   main.cpp         - SDL3 app callbacks (SDL_MAIN_USE_CALLBACKS).
//                      Thin glue: init -> core state -> view::draw.
//
// Error handling: every SDL call with a failure return is checked at
// the call site via backend::check(); fatal init failures also raise
// an SDL message box so a broken video driver is visible instead of
// a silent exit. See backend_sdl3.cpp.
//
// Threading: one background job at a time behind JobRunner, powered
// by std::jthread (RAII join, stop_token aware). The worker only
// produces a RunResult; the UI thread owns all state. No atomics,
// no detached threads, no data races by construction.
//
// ASCII-only literals: imgui default font covers ASCII only.

#pragma once

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

// Forward declarations keep SDL headers out of this header.
struct SDL_Window;
struct SDL_Renderer;

#ifndef WEMOD_ENHANCER_GUI_VERSION
#define WEMOD_ENHANCER_GUI_VERSION "0.0.0"
#endif

namespace wemod::gui
{

namespace fs = std::filesystem;

// --- shared constants (constexpr, no allocation) --------------------

constexpr std::string_view gui_version{WEMOD_ENHANCER_GUI_VERSION};

#ifdef _WIN32
constexpr bool is_windows{true};
#else
constexpr bool is_windows{false};
#endif

#if defined(__x86_64__) || defined(_M_X64)
constexpr std::string_view target_arch{"x86_64"};
#elif defined(__aarch64__) || defined(_M_ARM64)
constexpr std::string_view target_arch{"arm64"};
#else
constexpr std::string_view target_arch{"unknown"};
#endif

constexpr std::string_view patcher_script_name{"wemod_enhancer.py"};
constexpr std::string_view version_dll_name{"version.dll"};
constexpr std::string_view wemod_installer_url{
    "https://api.wemod.com/client/download"};
constexpr std::string_view launcher_clone_url{
    "https://github.com/DaniAsh551/wemod-launcher.git"};
constexpr std::string_view launcher_repo_url{
    "https://deckcheatz.com/wemod-on-linux-full-guide/"};
constexpr std::string_view issue_new_url{
    "https://github.com/e-gleba/wemod_enhancer/issues/new"};
constexpr std::size_t issue_log_budget{3000};
constexpr auto reprobe_interval{std::chrono::milliseconds(500)};
constexpr std::size_t log_budget{512UZ * 1024UZ};

constexpr std::string_view default_python{
    is_windows ? std::string_view("python") : std::string_view("python3")};

constexpr std::int32_t window_width_fallback{1024};
constexpr std::int32_t window_height_fallback{680};
constexpr std::int32_t window_min_width{880};
constexpr std::int32_t window_min_height{600};
constexpr std::int32_t window_max_width{1680};
constexpr std::int32_t window_max_height{1050};

// --- domain types ----------------------------------------------------

struct RunResult final
{
    std::int32_t exit_code{-1};
    std::string output;
};

enum class RunKind : std::uint8_t { patcher, probe, wemod };

enum class ProbeState : std::uint8_t { unknown, failed, works };

// Run a shell command, capture merged stdout+stderr and the exit code.
[[nodiscard]] RunResult run_capture(const std::string& command);

// One background job at a time. UI thread owns start()/try_take();
// the worker thread only fulfils the promise. std::jthread gives RAII
// join on destruction; a pending job is joined before relaunch.
class JobRunner final
{
  public:
    JobRunner() noexcept = default;
    ~JobRunner();
    JobRunner(const JobRunner&) = delete;
    JobRunner& operator=(const JobRunner&) = delete;
    JobRunner(JobRunner&&) = delete;
    JobRunner& operator=(JobRunner&&) = delete;

    [[nodiscard]] bool running() const noexcept { return active_; }
    void launch(std::string command);
    // Non-blocking harvest. nullopt = still running or idle. Never
    // throws: a worker exception becomes RunResult{-1, ...}.
    [[nodiscard]] std::optional<RunResult> try_take() noexcept;

  private:
    std::jthread worker_;
    std::future<RunResult> pending_;
    bool active_{false};
};

struct AppState final
{
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    std::string install_dir;
    std::string script_path;
    std::string python;
    std::string version_dll;
    std::string log;
    JobRunner job;
    RunKind kind{RunKind::patcher};
    bool scroll_to_bottom{false};
    bool has_run{false};
    std::int32_t last_exit_code{0};
    float copied_flash{0.0F};
    // Filesystem probe cache (throttled stat() round).
    std::string probed_install_dir;
    std::string probed_script_path;
    std::string probed_version_dll;
    fs::path resolved_install_dir;
    bool script_present{false};
    bool dll_present{false};
    std::chrono::steady_clock::time_point last_probe{};
    // Diagnostics.
    ProbeState python_ok{ProbeState::unknown};
    std::string python_version;
    std::string platform_detail;

    void append_log(std::string_view text);
    void start_command(RunKind next_kind,
                       const std::string& shown,
                       std::string command);
    void start_run(const char* subcommand);
    void start_probe();
    void start_wemod_download();
};

// --- backend: SDL3 only (backend_sdl3.cpp, noexcept where possible) ---

namespace backend
{

// SDL_Log wrapper (printf-style inside, plain string outside).
void log_error(const std::string& message) noexcept;
// Log + modal SDL message box. Never throws; a failing box only logs.
void show_error(const char* title,
                const std::string& message,
                SDL_Window* parent) noexcept;
// Check one SDL bool result: log on failure, optional fatal box.
// Returns ok unchanged so call sites read `if (!check(...))`.
[[nodiscard]] bool check(bool ok,
                         const char* what,
                         SDL_Window* parent,
                         bool fatal) noexcept;
[[nodiscard]] const char* env_var(const char* name) noexcept;
[[nodiscard]] fs::path exe_dir() noexcept;
[[nodiscard]] std::string platform_name() noexcept;
// Empty path when the folder is unknown (caller decides fallback).
[[nodiscard]] fs::path downloads_dir() noexcept;
// True on success; false logs (+ non-fatal box when parent set).
bool open_url(const char* url, SDL_Window* parent) noexcept;
bool set_clipboard(const std::string& text, SDL_Window* parent) noexcept;
void show_folder_dialog(AppState& state) noexcept;

struct WindowSize final
{
    std::int32_t width{window_width_fallback};
    std::int32_t height{window_height_fallback};
};
[[nodiscard]] WindowSize pick_window_size() noexcept;

} // namespace backend

// --- core: pure logic (core.cpp) -------------------------------------

namespace core
{

[[nodiscard]] std::string url_encode(std::string_view text);
[[nodiscard]] std::string shell_quote(std::string_view arg);
[[nodiscard]] constexpr std::vector<std::int32_t> version_parts(std::string name)
{
    constexpr std::string_view prefix{"app-"};
    if (name.starts_with(prefix)) {
        name.erase(0, prefix.size());
    }
    std::vector<std::int32_t> parts;
    std::size_t pos{0};
    while (pos < name.size()) {
        const std::size_t dot{name.find('.', pos)};
        const std::string_view token{
            name.data() + pos,
            (dot == std::string::npos ? name.size() : dot) - pos};
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
[[nodiscard]] fs::path newest_app_dir(const fs::path& root);
[[nodiscard]] std::string default_install_dir();
[[nodiscard]] fs::path resolve_wemod_dir(const std::string& dir);
[[nodiscard]] fs::path bundled_script();
[[nodiscard]] fs::path bundled_version_dll();
void probe_filesystem(AppState& state);
void parse_probe(AppState& state, const std::string& output);
void poll(AppState& state);
[[nodiscard]] std::string env_info(const AppState& state);
void copy_output(AppState& state);
void clear_output(AppState& state) noexcept;
void report_bug(AppState& state);
[[nodiscard]] std::string_view running_status(RunKind kind) noexcept;
[[nodiscard]] const char* run_block_reason(bool install_ok,
                                           bool script_ok) noexcept;

} // namespace core

// --- view: Dear ImGui only (view_imgui.cpp) --------------------------

namespace view
{

void draw(AppState& state);

} // namespace view

} // namespace wemod::gui
