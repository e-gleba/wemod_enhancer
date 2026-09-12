// model.hpp - Application Model: plain data + UI-thread intents.
// Knows platform:: (paths/env) and process:: (jobs) only through
// their narrow headers. Knows nothing about log sinks, SDL, ImGui.
// The presenter owns one Model and translates it into view::Snapshot.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace wemod::gui
{

namespace fs = std::filesystem;

#ifndef WEMOD_ENHANCER_GUI_VERSION
#define WEMOD_ENHANCER_GUI_VERSION "0.0.0"
#endif

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

enum class RunKind : std::uint8_t { patcher, probe, wemod };

enum class ProbeState : std::uint8_t { unknown, failed, works };

struct RunResult final
{
    std::int32_t exit_code{-1};
    std::string output;
};

// One background job at a time. UI thread owns launch()/try_take();
// the worker only fulfils the promise. std::jthread gives RAII join.
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
    void request_stop() noexcept;
    // Non-blocking harvest. nullopt = still running or idle.
    // Never throws: worker failure becomes Result{-1, ...}.
    [[nodiscard]] std::optional<RunResult> try_take() noexcept;

  private:
    std::jthread worker_;
    std::future<RunResult> pending_;
    bool active_{false};
};

// Application Model: owns fields, probe cache, job, log buffer.
// Methods are UI-thread intents (start_*) plus poll(). No SDL,
// no ImGui, no logging backend - only platform:: / process:: calls.
struct Model final
{
    std::string install_dir;
    std::string script_path;
    std::string python;
    std::string version_dll;
    std::string log;
    JobRunner job;
    RunKind kind{RunKind::patcher};
    bool scroll_to_bottom{false};
    bool has_run{false};
    // Set on close request while a job runs; the shell quits once
    // the job completes so shutdown never hides behind a join.
    bool quit_requested{false};
    std::int32_t last_exit_code{0};
    // Countdown (seconds) for the "Copied!" footer flash.
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
    void start_wemod_download(PlatformWindow* dialog_parent);
};

// Pure helpers (no Model, no IO beyond error_code filesys calls).
[[nodiscard]] std::string url_encode(std::string_view text);
[[nodiscard]] std::string shell_quote(std::string_view arg);
// Parse "app-1.2.3" (or "1.2.3") into numeric parts for ordering.
// Plain (non-constexpr): std::vector is not constant evaluable on
// all supported toolchains (notably MinGW libc++).
[[nodiscard]] std::vector<std::int32_t> version_parts(
    std::string_view name);
[[nodiscard]] fs::path newest_app_dir(const fs::path& root);
[[nodiscard]] std::string default_install_dir();
[[nodiscard]] fs::path resolve_wemod_dir(const std::string& dir);
[[nodiscard]] fs::path bundled_script();
[[nodiscard]] fs::path bundled_version_dll();
void probe_filesystem(Model& model);
void parse_probe(Model& model, const std::string& output);
void poll(Model& model);
[[nodiscard]] std::string env_info(const Model& model);
void copy_output(Model& model, PlatformWindow* parent);
void clear_output(Model& model) noexcept;
void report_bug(Model& model, PlatformWindow* parent);
[[nodiscard]] std::string_view running_status(RunKind kind) noexcept;
[[nodiscard]] const char* run_block_reason(bool install_ok,
                                           bool script_ok) noexcept;

} // namespace wemod::gui
