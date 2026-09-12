// App state: everything the UI needs. Owned by app.cpp via unique_ptr,
// threaded through the SDL callbacks. No SDL types here (window handle
// is void*): state.hpp must stay includable without SDL headers.
#pragma once

#include "config.hpp"
#include "runner.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace wemod::gui
{

namespace fs = std::filesystem;

struct app_state final
{
    void* window_handle{nullptr}; // SDL_Window*, opaque here
    std::string install_dir;
    std::string script_path;
    std::string python;
    std::string version_dll; // prefilled: the copy next to the exe
    std::string log;
    background_runner jobs;
    run_kind kind{run_kind::patcher};
    bool scroll_to_bottom{false};
    bool has_run{false};
    std::int32_t last_exit_code{0};
    float copied_flash{0.0F}; // seconds left of "Copied!" feedback
    // --- filesystem probe cache ---
    std::string probed_install_dir; // field text the last resolve ran on
    std::string probed_script_path; // ditto, for the script probe
    std::string probed_version_dll; // ditto, for the dll probe
    fs::path resolved_install_dir;  // cached resolve result
    bool script_present{false};     // the patcher script exists on disk
    bool dll_present{false};        // version.dll field path exists on disk
    std::chrono::steady_clock::time_point last_probe; // last stat() round
    // --- diagnostics ---
    probe_state python_ok{probe_state::unknown};
    std::string python_version;  // e.g. "Python 3.13.5"
    std::string platform_detail; // e.g. "Windows-11-10.0.26200"
};

void append_log(app_state& state, std::string_view text);

// Re-resolve the WeMod folder + script/dll presence. Throttled: edits
// re-probe immediately, reprobe_interval catches on-disk changes.
void probe_filesystem(app_state& state);

// The folder the patcher wants: app-x.y.z holding resources/app.asar.
// Accepts that folder directly, the WeMod root above it (newest app-*
// inside), or the wemod-launcher clone (wemod_data/wemod_bin inside).
[[nodiscard]] fs::path resolve_wemod_dir(const std::string& dir);

// Launch a background command; `shown` is the exact command line the
// user sees in the log (no shorthand). No-op while a job runs.
void start_command(app_state& state, run_kind kind, const std::string& shown,
                   std::vector<std::string> argv);

// Run the patcher with the current field values.
void start_run(app_state& state, const char* subcommand);

// "Download WeMod": Windows fetches the official installer into
// Downloads and runs it; Linux clones wemod-launcher and opens the tutorial.
void start_wemod_download(app_state& state);

// Probe the Python on PATH once at startup.
void start_probe(app_state& state);

// Drain a finished background job into the log. Call every frame.
void poll_run(app_state& state);

// Environment block for the bug report: versions and paths, no secrets.
[[nodiscard]] std::string env_info(const app_state& state);

void copy_output(app_state& state);
void clear_output(app_state& state);
void report_bug(app_state& state);

} // namespace wemod::gui
