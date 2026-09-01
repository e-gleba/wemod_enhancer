// WeMod Enhancer - desktop app (Dear ImGui + SDL3, C++23).
//
// A click-button wrapper around the Python patcher
// (scripts/wemod_enhancer.py): it picks the WeMod folder, runs it in
// a terminal and shows stdout/stderr live, plus the exit code, in a
// scrolling log.
//
// The patcher ships inside the package: wemod_enhancer.py and
// version.dll sit next to the executable. No download, no update
// check, no network - the install is one self-contained movable
// folder. Required: Python 3.11+ on PATH and the WeMod folder
// (auto-detected at startup, so usually just press Patch).
//
// When the WeMod folder is missing/invalid, the "Download WeMod"
// button does the real thing, no dialogs:
//   - Windows: downloads the official installer (api.wemod.com) into
//     the user Downloads folder via SDL_GetUserFolder and runs it.
//   - Linux:   git-clones the wemod-launcher repo into ~/wemod-launcher
//     and opens the DeckCheatz tutorial in the browser. After the
//     first run + login, wemod_data/wemod_bin appears and the
//     folder field resolves to it automatically.
//
// Structure: SDL3 app callbacks (SDL_MAIN_USE_CALLBACKS is set via
// CMake), one frame = poll the background command + draw the UI.
//
// Layout (default imgui theme, untouched colors - hierarchy comes
// from alignment, padding and scale):
//   - WeMod folder field with Browse on the same row.
//   - Full-width action row under the path: Patch / Restore, plus
//     Download WeMod only while the folder is unresolved. Buttons
//     share the row equally. Same height as Copy / Clear / Report
//     so every full-span row reads as one control language.
//   - Status line, then Settings as a collapsing header (collapsed
//     by default, full width - Python / patcher / version.dll).
//   - Log fills the rest. Copy output / Clear output / Report bug
//     share one equal-width full-span row; Copied! left and the
//     version centered on the footer line under it.
//   - Window size is a fraction of the usable display (clamped) so
//     FHD / 1440p / 4K all open comfortably; FontScaleDpi scales
//     every font-size-derived widget.
//
// C++ Core Guidelines, applied where they cost nothing:
//   - No owning raw pointers, no new/delete, no C casts; const and
//     #ifdef wherever both branches compile; the preprocessor only
//     remains where names do not exist cross-platform (popen/pclose).
//   - SDL3 owns the platform glue: SDL_GetEnvironmentVariable instead
//     of std::getenv, SDL_GetBasePath for the exe dir the patcher
//     lives next to, SDL_GetUserFolder for Downloads, SDL_GetPlatform
//     for diagnostics.
//   - std::filesystem resolves the install layout: exe_dir()
//     (SDL_GetBasePath) is the single anchor, script and DLL are
//     siblings of the executable.
//   - gsl::not_null for pointers out of C callbacks, Expects() for
//     preconditions; app state ownership is a std::unique_ptr
//     (make_unique in SDL_AppInit, re-acquired in SDL_AppQuit) - no
//     manual delete anywhere.
//   - No C-style strings in logic; std::string / std::string_view.
//   - C++23 library over hand-rolled loops: std::ranges::max_element
//     (newest version dir), std::ranges::all_of (tag validation).
//   - std::format for every formatted string (type-safe, no printf).
//   - std::async (RAII: the future joins on destruction) instead of
//     std::thread + std::atomic; no detached threads, no data races:
//     the worker only returns a string, the UI thread owns the log.
//   - No global state: one app_state struct threaded through the SDL
//     callbacks; constants are constexpr in an anonymous namespace.
//   - enum class for state, not bare ints/bools.
//   - std::error_code filesystem overloads - directory walking must
//     not throw on a half-installed WeMod.
//   - ImGui widget widths are computed from the font size
//     (CalcTextSize / GetFrameHeight), so nothing clips at any DPI.
//   - One scale factor for the whole UI: style.FontScaleDpi at init
//     scales every font-size-derived widget (ImGui 1.92+); the window
//     is created at logical size, so SDL keeps the physical size
//     constant across DPIs.
//   - Folder validity is an INVARIANT of the current field text:
//     resolve_wemod_dir() re-runs on edits and a 500 ms timer (never
//     every frame - it walks directories), so the field can never
//     look "ok" while Patch refuses to run.
//   - The log is capped (oldest lines dropped past the budget) -
//     ImGui re-renders the whole string every frame.
//   - The log shows the REAL command line for every background job
//     (git clone ..., python ...) - no shorthand, no "->", so the
//     user sees exactly what ran and can reproduce it.
//
// NOTE: imgui's default font covers ASCII only - keep every literal in
// this file plain ASCII (no em-dashes, arrows or ellipsis characters).

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_stdlib.h>

#include <gsl/narrow>
#include <gsl/pointers>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h> // WIFEXITED / WEXITSTATUS for pclose()
#endif

namespace
{

namespace fs = std::filesystem;

// GUI version: the CMake project version, injected at compile time -
// the GUI can never show a version that differs from its package.
constexpr std::string_view gui_version{WEMOD_ENHANCER_GUI_VERSION};

// Compile-time platform facts. The preprocessor stays only where the
// names do not exist on the other platform (popen/pclose); everything
// else is ordinary C++ selected with `if constexpr`.
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

// CMake installs gui + script + dll into the same bindir.
constexpr std::string_view patcher_script_name{"wemod_enhancer.py"};
constexpr std::string_view version_dll_name{"version.dll"};

// "Download WeMod" endpoints. Windows: the official installer direct
// link (what https://www.wemod.com/download serves). Linux: the
// wemod-launcher repo + the DeckCheatz tutorial from the readme.
constexpr std::string_view wemod_installer_url{
    "https://api.wemod.com/client/download"};
constexpr std::string_view launcher_clone_url{
    "https://github.com/DaniAsh551/wemod-launcher.git"};
constexpr std::string_view launcher_repo_url{
    "https://deckcheatz.com/wemod-on-linux-full-guide/"};

// "Report bug" target; the log tail rides in the issue body.
constexpr std::string_view issue_new_url{
    "https://github.com/e-gleba/wemod_enhancer/issues/new"};
constexpr std::size_t issue_log_budget{3000};

// stat() is not free at 60 fps: edits re-probe immediately, this
// timer catches on-disk changes (e.g. the "Download WeMod" flow).
constexpr auto reprobe_interval{std::chrono::milliseconds(500)};

// Past this budget the oldest log lines are dropped at a newline.
// UZ: multiply in std::size_t, no int-to-size_t widening of the product.
constexpr std::size_t log_budget{512UZ * 1024UZ};

constexpr std::string_view default_python{
    is_windows ? std::string_view("python") : std::string_view("python3")};

// Fallback logical window size when the display cannot be queried.
// pick_window_size() uses a fraction of the usable display so FHD /
// 1440p / 4K all get a comfortable default; FontScaleDpi then scales
// every font-size-derived widget. SDL keeps the physical size
// constant across DPIs because the window is created at logical size.
constexpr std::int32_t window_width_fallback{1024};
constexpr std::int32_t window_height_fallback{680};
constexpr std::int32_t window_min_width{880};
constexpr std::int32_t window_min_height{600};
constexpr std::int32_t window_max_width{1680};
constexpr std::int32_t window_max_height{1050};

// Extra horizontal padding on the Browse button so it matches the
// path field's visual weight. Action buttons ignore this - they
// stretch to share the full row.
constexpr float button_padding{24.0F};
constexpr float section_indent{16.0F};

// One height for every full-span button row (Patch / Restore and
// Copy / Clear / Report). Same scale = one control language.
// Font-size derived via GetFrameHeight().
constexpr float row_height_scale{1.55F};

constexpr ImVec4 clear_color{0.10F, 0.10F, 0.12F, 1.00F};

// Status colors (replacing the magic-number literals).
constexpr ImVec4 color_ok{0.35F, 0.85F, 0.45F, 1.00F};
constexpr ImVec4 color_err{0.90F, 0.30F, 0.30F, 1.00F};

// Input field tint when the path is valid: quiet green fill.
constexpr ImVec4 field_ok_bg{0.14F, 0.32F, 0.16F, 0.70F};

struct run_result final
{
    std::int32_t exit_code{-1};
    std::string output;
};

// What the current background command is, so poll_run() can react to
// completion: record the Python probe result, narrate the WeMod fetch,
// hint after a failed patch.
enum class run_kind : std::uint8_t { patcher, probe, wemod };
static_assert(std::is_enum_v<run_kind>);

// Python probe tri-state: unknown / failed / works.
enum class probe_state : std::uint8_t { unknown, failed, works };
static_assert(std::is_enum_v<probe_state>);

// Everything the UI needs, owned by main via unique_ptr.
struct app_state final
{
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    std::string install_dir;
    std::string script_path;
    std::string python;
    std::string version_dll; // empty = auto: the copy next to the exe
    std::string log;
    std::future<run_result> pending;
    bool running{false};
    run_kind kind{run_kind::patcher};
    bool scroll_to_bottom{false};
    bool has_run{false};
    std::int32_t last_exit_code{0};
    float copied_flash{0.0F}; // seconds left of "Copied!" feedback
    // --- filesystem probe cache (see probe_filesystem) ---
    std::string probed_install_dir; // field text the last resolve ran on
    std::string probed_script_path; // ditto, for the script probe
    fs::path resolved_install_dir;  // cached resolve_wemod_dir result
    bool script_present{false};     // the bundled script exists on disk
    std::chrono::steady_clock::time_point last_probe; // last stat() round
    // --- diagnostics ---
    probe_state python_ok{probe_state::unknown};
    std::string python_version;  // e.g. "Python 3.13.5"
    std::string platform_detail; // e.g. "Windows-11-10.0.26200"
};

// gsl::not_null at the C-callback boundary: SDL guarantees appstate.
[[nodiscard]] app_state& state_of(void* appstate) noexcept
{
    return *gsl::make_not_null(static_cast<app_state*>(appstate));
}

// --- tiny helpers (std::format, no printf-family) -------------------

// One colored line. TextUnformatted: the text is never a format string.
void text_colored(const ImVec4& color, const std::string_view text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopStyleColor();
}

// Muted helper text (paths, hints). Wrapped: long paths must not
// shove the layout sideways.
void text_disabled(const std::string_view text)
{
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// Hover tooltip; the (?) markers and the version label share it.
void tooltip_text(const std::string_view text)
{
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0F);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// SDL_Log already formats; forwarding through std::format would only
// risk a brace in the message being parsed as a replacement field.
void sdl_log_error(const std::string& message)
{
    SDL_Log("%s", message.c_str()); // NOLINT(cppcoreguidelines-pro-type-vararg)
}

// Append to the log, dropping the oldest lines past the budget.
void append_log(app_state& state, const std::string_view text)
{
    state.log += text;
    if (state.log.size() > log_budget) {
        const std::size_t over{state.log.size() - log_budget};
        const std::size_t nl{state.log.find('\n', over)};
        state.log.erase(0, nl == std::string::npos ? over : nl + 1);
    }
}

// Process environment via SDL3 (no std::getenv, no CRT quirks).
[[nodiscard]] const char* env_var(const char* name) noexcept
{
    return SDL_GetEnvironmentVariable(SDL_GetEnvironment(), name);
}

// RFC 3986 percent-encoding (unreserved characters pass through), for
// the pre-filled GitHub issue URL behind the Report bug button.
[[nodiscard]] std::string url_encode(const std::string_view text)
{
    constexpr std::string_view unreserved{
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~"};
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

// Quote one argument for the shell that runs our background commands:
// cmd.exe on Windows (double quotes, "" escapes a quote), /bin/sh
// elsewhere (single quotes, '"''"'"' escapes a quote).
[[nodiscard]] std::string shell_quote(const std::string_view arg)
{
    if constexpr (is_windows) {
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

// Run a command, capture merged stdout+stderr and the exit code.
// _popen/_pclose exist only on Windows - the one place the
// preprocessor is unavoidable. 2>&1 is a no-op under cmd.exe (it
// errors on the syntax), so Windows gets stdout only.
[[nodiscard]] run_result run_capture(const std::string& command)
{
    run_result result;
    const std::string full{is_windows ? command : command + " 2>&1"};

#ifdef _WIN32
    FILE* pipe{_popen(full.c_str(), "r")};
#else
    FILE* pipe{popen(full.c_str(), "r")};
#endif
    if (pipe == nullptr) {
        result.output =
            std::format("error: failed to start the command ({})",
                        std::system_category().message(errno));
        return result;
    }

    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), gsl::narrow<int>(buffer.size()), pipe) !=
           nullptr) {
        result.output += buffer.data();
    }

#ifdef _WIN32
    result.exit_code = _pclose(pipe);
#else
    const int status{pclose(pipe)};
    result.exit_code =
        status == -1 || !WIFEXITED(status) ? -1 : WEXITSTATUS(status);
#endif
    return result;
}

// "app-10.2.3" -> {10, 2, 3}; non-numeric tokens become 0.
[[nodiscard]] constexpr std::vector<std::int32_t>
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

// Newest app-x.y.z directory inside a WeMod root (lexicographic would
// order app-10 before app-9). Empty path when none.
[[nodiscard]] fs::path newest_app_dir(const fs::path& root)
{
    std::error_code ec;
    std::vector<fs::path> apps;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_directory(ec) &&
            entry.path().filename().string().starts_with("app-")) {
            apps.push_back(entry.path());
        }
    }
    const auto newest{std::ranges::max_element(
        apps, {}, [](const fs::path& path) {
            return version_parts(path.filename().string());
        })};
    return newest == apps.end() ? fs::path{} : *newest;
}

// WeMod's default install dir for the current platform, or empty.
[[nodiscard]] std::string default_install_dir()
{
    if constexpr (is_windows) {
        if (const char* local{env_var("LOCALAPPDATA")}) {
            return (fs::path(local) / "WeMod").string();
        }
    } else {
        if (const char* home{env_var("HOME")}) {
            return (fs::path(home) / "wemod-launcher").string();
        }
    }
    return {};
}

// The folder the patcher wants: app-x.y.z holding resources/app.asar.
// Accepts that folder directly, the WeMod root above it (newest app-*
// inside), or the wemod-launcher clone (wemod_data/wemod_bin inside).
// Single validity invariant: re-run on edits and on the reprobe timer.
[[nodiscard]] fs::path resolve_wemod_dir(const std::string& dir)
{
    if (dir.empty()) {
        return {};
    }
    std::error_code ec;
    fs::path picked{dir};
    // Direct hit: the app-x.y.z folder itself.
    if (fs::is_regular_file(picked / "resources" / "app.asar", ec)) {
        return picked;
    }
    // WeMod root: newest app-x.y.z inside.
    if (const fs::path app{newest_app_dir(picked)}; !app.empty()) {
        return app;
    }
    // wemod-launcher clone: wemod_data/wemod_bin inside (appears after
    // the first run + login - see the readme tutorial).
    fs::path launcher_bin{picked / "wemod_data" / "wemod_bin"};
    if (fs::is_regular_file(launcher_bin / "resources" / "app.asar", ec)) {
        return launcher_bin;
    }
    return {};
}

// Throttled probes behind the field validity invariant: edits re-probe
// immediately, reprobe_interval catches on-disk changes.
void probe_filesystem(app_state& state)
{
    const auto now{std::chrono::steady_clock::now()};
    if (state.probed_install_dir == state.install_dir &&
        state.probed_script_path == state.script_path &&
        (now - state.last_probe) < reprobe_interval) {
        return;
    }
    state.probed_install_dir = state.install_dir;
    state.probed_script_path = state.script_path;
    state.last_probe = now;
    state.resolved_install_dir = resolve_wemod_dir(state.install_dir);
    // error_code overload: a malformed path in the field must not throw.
    std::error_code ec;
    state.script_present = !state.script_path.empty() &&
        fs::is_regular_file(state.script_path, ec);
}

// SDL dialog callback: may run on another thread; it only writes a
// std::string that the UI thread reads next frame - safe in practice
// because the dialog is modal and the field is not edited meanwhile.
void SDLCALL on_folder_chosen(void* userdata,
                              const char* const* filelist,
                              int /*filter*/)
{
    auto& state{state_of(userdata)};
    if (filelist != nullptr && filelist[0] != nullptr) {
        state.install_dir = filelist[0];
    }
}

// Dir the running exe sits in. SDL_GetBasePath picks it on every OS;
// the patcher is installed next to the exe, so the whole install stays
// one self-contained, movable folder. The result is SDL-owned internal
// memory (const char*, cached internally) - unlike SDL_GetPrefPath it
// must NOT be freed, so plain pointer it is.
[[nodiscard]] fs::path exe_dir()
{
    if (const char* base{SDL_GetBasePath()}) {
        return {base};
    }
    return fs::temp_directory_path() / "wemod_enhancer";
}

// The bundled patcher script: a sibling of the executable.
[[nodiscard]] fs::path bundled_script()
{
    return exe_dir() / patcher_script_name;
}

// The bundled proxy DLL, reported only when it exists on disk.
[[nodiscard]] fs::path bundled_version_dll()
{
    std::error_code ec;
    fs::path dll{exe_dir() / version_dll_name};
    if (fs::is_regular_file(dll, ec)) {
        return dll;
    }
    return {};
}

// Launch a background command and stream its output into the log.
// `shown` is the real command line the user sees - no shorthand.
void start_command(app_state& state,
                   const run_kind kind,
                   const std::string& shown,
                   const std::string& command)
{
    Expects(!command.empty());
    if (state.running) {
        return;
    }
    append_log(state, "$ " + shown + "\n");
    state.kind = kind;
    state.running = true;
    if (kind == run_kind::patcher) {
        state.has_run = true;
    }
    state.pending = std::async(std::launch::async,
                               [command] { return run_capture(command); });
}

// Run the patcher with the current field values. `shown` mirrors the
// real command line (quoting included) so the log is reproducible.
void start_run(app_state& state, const char* subcommand)
{
    Expects(subcommand != nullptr);
    std::string shown{state.python + " -u " + state.script_path + " " +
                      subcommand + " --install-dir " + state.install_dir};
    std::string command{shell_quote(state.python) + " -u " +
                        shell_quote(state.script_path) + " " + subcommand +
                        " --install-dir " + shell_quote(state.install_dir)};
    if (std::string_view(subcommand) == "patch") {
        // Field override wins; else the bundled DLL next to the exe.
        const std::string dll{!state.version_dll.empty()
                                  ? state.version_dll
                                  : bundled_version_dll().string()};
        if (!dll.empty()) {
            shown += " --version-dll " + dll;
            command += " --version-dll " + shell_quote(dll);
        }
    }
    start_command(state, run_kind::patcher, shown, command);
}

// "Download WeMod" does the real thing, no dialogs:
//   Windows - fetch the official installer into the user Downloads
//             folder (SDL_GetUserFolder, no hardcoded paths) and run
//             it. The installer registers the app; the next frame
//             re-detects the folder.
//   Linux   - git-clone the wemod-launcher into ~/wemod-launcher and
//             open the tutorial; after the first run + login the
//             folder field resolves to wemod_data/wemod_bin.
void start_wemod_download(app_state& state)
{
    if constexpr (is_windows) {
        // SDL_GetUserFolder returns SDL-owned memory - do not free.
        const char* const downloads{SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)};
        if (downloads == nullptr) {
            sdl_log_error(std::string("SDL_GetUserFolder: ") +
                          SDL_GetError());
            return;
        }
        const fs::path installer{fs::path(downloads) / "wemod_setup.exe"};
        const std::string command{
            "powershell -NoProfile -ExecutionPolicy Bypass -Command \""
            "$ProgressPreference='SilentlyContinue'; "
            "Invoke-WebRequest -Uri '" +
            std::string(wemod_installer_url) + "' -OutFile '" +
            installer.string() + "'; Start-Process '" +
            installer.string() + "'\""};
        start_command(state, run_kind::wemod, command, command);
    } else {
        const char* home{env_var("HOME")};
        if (home == nullptr) {
            append_log(state,
                       "error: HOME is not set - cannot clone "
                       "wemod-launcher.\n\n");
            return;
        }
        const fs::path dir{fs::path(home) / "wemod-launcher"};
        std::error_code ec;
        if (fs::is_directory(dir, ec)) {
            // Already cloned: just aim the field at it - the resolver
            // picks up wemod_data/wemod_bin once login happened.
            state.install_dir = dir.string();
            append_log(state,
                       "wemod-launcher already cloned: " + dir.string() +
                           "\n  run it once and log in - wemod_data/wemod_bin "
                           "appears after login, this field resolves to "
                           "it.\n\n");
            state.scroll_to_bottom = true;
            return;
        }
        // Tutorial opens alongside the clone, per the readme flow.
        if (!SDL_OpenURL(std::string(launcher_repo_url).c_str())) {
            sdl_log_error(std::string("SDL_OpenURL(tutorial): ") +
                          SDL_GetError());
        }
        const std::string command{"git clone " +
                                  std::string(launcher_clone_url) + " " +
                                  shell_quote(dir.string())};
        start_command(state, run_kind::wemod, command, command);
    }
}

// Probe the Python on PATH once at startup: the Patch button and the
// env info both need to know it works before anything runs.
void start_probe(app_state& state)
{
    const std::string shown{state.python +
                            " -c \"import sys,platform;print(sys.version."
                            "split()[0]);print(platform.platform())\""};
    start_command(state, run_kind::probe, shown,
                  shell_quote(state.python) +
                      " -c \"import sys,platform;print(sys.version.split()[0"
                      "]);print(platform.platform())\"");
}

// Probe output: version on line 1, platform on line 2. Both ride in
// the bug report, so keep them raw (trimmed).
void parse_probe(app_state& state, const std::string& output)
{
    const auto line = [&](const std::size_t from) {
        const std::size_t eol{output.find('\n', from)};
        std::string_view text{output.data() + from,
                              (eol == std::string::npos ? output.size()
                                                        : eol) -
                                  from};
        while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) {
            text.remove_suffix(1);
        }
        return std::string(text);
    };
    const std::string version{line(0)};
    if (!version.empty()) {
        state.python_version = "Python " + version;
    }
    if (const std::size_t eol{output.find('\n')};
        eol != std::string::npos) {
        state.platform_detail = line(eol + 1);
    }
}

void poll_run(app_state& state)
{
    if (!state.running ||
        state.pending.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
        return;
    }
    const run_result result{state.pending.get()};
    append_log(state, result.output);
    if (!result.output.empty() && !result.output.ends_with('\n')) {
        append_log(state, "\n");
    }
    append_log(state,
               "[exit code: " + std::to_string(result.exit_code) + "]\n\n");
    state.last_exit_code = result.exit_code;
    state.running = false;
    state.scroll_to_bottom = true;

    // Every failure says what happened and how to fix it - the log is
    // the error report (see the Copy output / Report bug buttons).
    switch (state.kind) {
    case run_kind::wemod:
        if (result.exit_code != 0) {
            append_log(
                state,
                is_windows
                    ? "error: could not download the WeMod installer.\n"
                      "  fix: check the network connection, then retry - or "
                      "grab it from https://www.wemod.com/download\n\n"
                    : "error: could not clone wemod-launcher.\n"
                      "  fix: check the network connection and that git is "
                      "installed, then retry.\n\n");
            break;
        }
        if constexpr (is_windows) {
            append_log(state,
                       "WeMod installer downloaded and started.\n"
                       "  next: install, run WeMod once, log in - then "
                       "this app auto-detects the folder.\n\n");
        } else {
            // Aim the field at the fresh clone: after the first run +
            // login the resolver picks wemod_data/wemod_bin inside it.
            if (const char* home{env_var("HOME")}) {
                state.install_dir =
                    (fs::path(home) / "wemod-launcher").string();
            }
            append_log(state,
                       "wemod-launcher cloned (tutorial opened in your "
                       "browser).\n"
                       "  next: run it once and log in - "
                       "wemod_data/wemod_bin appears after login, the "
                       "folder field resolves to it.\n\n");
        }
        break;
    case run_kind::probe:
        state.python_ok = result.exit_code == 0 ? probe_state::works
                                                : probe_state::failed;
        parse_probe(state, result.output);
        break;
    case run_kind::patcher:
        if (result.exit_code != 0) {
            append_log(state,
                       "hint: close WeMod fully, then retry. If it still "
                       "fails, press Report bug below - the issue opens "
                       "pre-filled with this log.\n\n");
        }
        break;
    }
}

// Environment block for the bug report: versions and paths, no
// secrets (install dir may contain the user name - that is exactly
// what a log needs).
[[nodiscard]] std::string env_info(const app_state& state)
{
    std::string info{"--- environment ---\n"};
    info += "gui version: ";
    info += std::string(gui_version) + "\n";
    info += "platform: " + std::string(SDL_GetPlatform()) + " " +
        std::string(target_arch) + "\n";
    // The exe dir anchors the bundled patcher - a bug report must show
    // where the package was unpacked.
    info += "exe dir: " + exe_dir().string() + "\n";
    info += "wemod folder: " +
        (state.install_dir.empty() ? std::string("<not set>")
                                   : state.install_dir) +
        "\n";
    info += "patcher script: " +
        (state.script_path.empty() ? std::string("<missing next to exe>")
                                   : state.script_path) +
        "\n";
    info += "python command: " + state.python + "\n";
    info += "python: " +
        (state.python_version.empty() ? std::string("<not probed>")
                                      : state.python_version) +
        "\n";
    if (!state.platform_detail.empty()) {
        info += "os: " + state.platform_detail + "\n";
    }
    if (state.has_run) {
        info +=
            "last exit code: " + std::to_string(state.last_exit_code) + "\n";
    }
    return info;
}

// Copy the log + environment to the clipboard (SDL owns the text).
void copy_output(app_state& state)
{
    if (!SDL_SetClipboardText((state.log + "\n" + env_info(state)).c_str())) {
        sdl_log_error(std::string("SDL_SetClipboardText: ") +
                      SDL_GetError());
        return;
    }
    state.copied_flash = 1.5F;
}

// Drop the log. The next patch run fills it again.
void clear_output(app_state& state)
{
    state.log.clear();
    state.copied_flash = 0.0F;
}

// Open a pre-filled bug report: the log tail and the environment ride
// in the body. SDL_OpenURL handles the platform browser.
void report_bug(app_state& state)
{
    std::string body{"## log\n\n```\n"};
    if (state.log.size() > issue_log_budget) {
        body += "... (log tail)\n";
        body += state.log.substr(state.log.size() - issue_log_budget);
    } else {
        body += state.log;
    }
    body += "```\n\n" + env_info(state);
    const std::string url{std::string(issue_new_url) +
                          "?template=bug_report.yml&title=" +
                          url_encode("bug: gui report") +
                          "&body=" + url_encode(body)};
    if (!SDL_OpenURL(url.c_str())) {
        sdl_log_error(std::string("SDL_OpenURL: ") + SDL_GetError());
    }
}

// Comfortable logical window size: a fraction of the usable display,
// clamped so laptops stay usable and 4K does not open a postage stamp.
// HiDPI is FontScaleDpi's job - this only picks the window.
[[nodiscard]] std::pair<std::int32_t, std::int32_t> pick_window_size()
{
    SDL_Rect usable{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) ||
        usable.w <= 0 || usable.h <= 0) {
        return {window_width_fallback, window_height_fallback};
    }
    const std::int32_t width{std::clamp(usable.w / 2, window_min_width,
                                        window_max_width)};
    const std::int32_t height{std::clamp((usable.h * 3) / 5,
                                         window_min_height,
                                         window_max_height)};
    return {width, height};
}

// --- layout helpers (font-size derived, DPI-correct) ------------------

// Button at an explicit size. Height 0 keeps the default frame
// height (path-row Browse). Returns true when clicked.
bool action_button(const char* label, const float width, const float height)
{
    Expects(label != nullptr);
    return ImGui::Button(label, ImVec2(width, height));
}

// Equal slice of the current row for `count` sibling buttons, gaps
// included. Stretch-to-fill: action and utility rows always span
// the window.
[[nodiscard]] float equal_button_width(const int count)
{
    Expects(count > 0);
    const ImGuiStyle& style{ImGui::GetStyle()};
    const float avail{ImGui::GetContentRegionAvail().x};
    const float gaps{style.ItemSpacing.x * static_cast<float>(count - 1)};
    return std::max((avail - gaps) / static_cast<float>(count), 1.0F);
}

// Small dim section label above a field.
void field_label(const char* label)
{
    Expects(label != nullptr);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

// "(?)" hover marker with a tooltip; `url` opens on click.
void help_marker(const char* text, const char* url)
{
    Expects(text != nullptr);
    Expects(url != nullptr);
    ImGui::SameLine();
    text_disabled("(?)");
    if (ImGui::IsItemHovered()) {
        tooltip_text(text);
    }
    if (ImGui::IsItemClicked()) {
        if (!SDL_OpenURL(url)) {
            sdl_log_error(std::string("SDL_OpenURL: ") + SDL_GetError());
        }
    }
}

// Why Patch is disabled, or nullptr when it can run.
[[nodiscard]] const char* patch_block_reason(const bool install_ok,
                                             const bool script_ok)
{
    if (!install_ok && !script_ok) {
        return "Needs a valid WeMod folder and the patcher";
    }
    if (!install_ok) {
        return "Pick a valid WeMod folder first";
    }
    if (!script_ok) {
        return "The bundled patcher is missing - see Settings";
    }
    return nullptr;
}

// Status line text while a background command runs.
[[nodiscard]] std::string_view running_status(const run_kind kind) noexcept
{
    switch (kind) {
    case run_kind::probe:
        return "Checking Python...";
    case run_kind::wemod:
        return is_windows ? std::string_view("Downloading WeMod...")
                          : std::string_view("Cloning wemod-launcher...");
    case run_kind::patcher:
        return "Running the patcher...";
    }
    return {};
}

// Settings body: patcher / Python / version.dll. Inputs stretch.
void draw_settings(app_state& state, const bool script_ok)
{
    field_label("Patcher");
    help_marker(
        "wemod_enhancer.py + version.dll ship inside this package, "
        "next to the executable - no download, no update step. If the "
        "script is reported missing, re-download the GUI package.",
        "https://github.com/e-gleba/wemod_enhancer/releases/latest");
    ImGui::SameLine();
    if (script_ok) {
        text_colored(color_ok, "ready");
        ImGui::SameLine();
        text_disabled(state.script_path);
    } else {
        text_colored(color_err, "missing next to the exe");
    }

    ImGui::Spacing();

    field_label("Python command");
    help_marker(
        "The interpreter that runs the patcher. Default: python on "
        "Windows, python3 elsewhere. Point it at a full path if "
        "Python is not on PATH.",
        "https://www.python.org/downloads/");
    ImGui::SameLine();
    if (state.python_ok == probe_state::works) {
        text_colored(color_ok, state.python_version);
    } else if (state.python_ok == probe_state::failed) {
        text_colored(color_err, "not working");
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##python", &state.python);

    ImGui::Spacing();

    field_label("version.dll");
    help_marker(
        "The proxy DLL the patcher drops next to WeMod. Leave empty "
        "to use the copy bundled next to the executable.",
        "https://github.com/e-gleba/wemod_enhancer#wemod-enhancer");
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##version_dll",
                             "auto: next to the exe",
                             &state.version_dll);
}

// The whole window: path+Browse, equal-width action row, status,
// collapsing Settings, scrolling log, equal-width Copy/Clear/Report.
// One frame = one draw call.
void draw_ui(app_state& state)
{
    const ImGuiViewport* viewport{ImGui::GetMainViewport()};
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags window_flags{
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings};

    ImGui::Begin("##main", nullptr, window_flags);

    const ImGuiStyle& style{ImGui::GetStyle()};
    const float row_h{ImGui::GetFrameHeight() * row_height_scale};

    probe_filesystem(state);
    const fs::path& resolved_dir{state.resolved_install_dir};
    const bool install_ok{!resolved_dir.empty()};
    const bool script_ok{state.script_present};

    // --- WeMod folder + Browse on one row -----------------------------
    // Validity is an invariant of the current field text, re-probed on
    // edits and on the reprobe timer (never every frame - the resolve
    // walks directories). Patch normalizes the field to the resolved
    // dir so the log always shows the exact folder used.
    field_label("WeMod folder");
    help_marker(
        "The app-x.y.z folder with resources/app.asar inside. Pick the "
        "WeMod root and the newest version is used automatically. "
        "Linux: the wemod-launcher clone works too - after the first "
        "run + login its wemod_data/wemod_bin is picked up.",
        "https://github.com/e-gleba/wemod_enhancer#quick-start");
    ImGui::SameLine();
    if (!state.install_dir.empty()) {
        text_colored(install_ok ? color_ok : color_err,
                     install_ok ? "ok" : "not a WeMod install");
    }

    const float browse_w{ImGui::CalcTextSize("Browse...").x +
                         (style.FramePadding.x * 2.0F) + button_padding};
    const float path_w{std::max(ImGui::GetContentRegionAvail().x - browse_w -
                                    style.ItemSpacing.x,
                                ImGui::GetFontSize() * 8.0F)};

    if (install_ok) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, field_ok_bg);
    }
    ImGui::SetNextItemWidth(path_w);
    ImGui::InputTextWithHint("##install_dir", "path to WeMod",
                             &state.install_dir);
    if (install_ok) {
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (action_button("Browse...", browse_w, 0.0F)) {
        SDL_ShowOpenFolderDialog(on_folder_chosen, &state, state.window,
                                 state.install_dir.empty()
                                     ? nullptr
                                     : state.install_dir.c_str(),
                                 false);
    }

    if (install_ok && resolved_dir.string() != state.install_dir) {
        text_disabled(resolved_dir.string());
    }

    // --- Actions: equal-width row, full span under the path -----------
    // Count = 2 (Patch, Restore) or 3 (+ Download WeMod while the
    // folder is unresolved). Every button gets the same slice so the
    // row is one alignment axis, whatever the labels. Height matches
    // the Copy / Clear / Report row below.
    ImGui::Spacing();
    const int action_count{install_ok ? 2 : 3};
    const float action_w{equal_button_width(action_count)};

    const char* block_reason{patch_block_reason(install_ok, script_ok)};
    const bool blocked{state.running || block_reason != nullptr};
    ImGui::BeginDisabled(blocked);
    if (action_button("Patch", action_w, row_h)) {
        // Normalize the field to the resolved dir: the log then shows
        // the exact folder the patcher ran against.
        state.install_dir = resolved_dir.string();
        start_run(state, "patch");
    }
    ImGui::EndDisabled();
    if (block_reason != nullptr &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip_text(block_reason);
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(state.running);
    if (action_button("Restore", action_w, row_h)) {
        start_run(state, "restore");
    }
    ImGui::EndDisabled();

    if (!install_ok) {
        ImGui::SameLine();
        if (action_button("Download WeMod", action_w, row_h)) {
            start_wemod_download(state);
        }
        if (ImGui::IsItemHovered()) {
            tooltip_text(is_windows
                             ? "Download the official WeMod installer into "
                               "your Downloads folder and run it"
                             : "Clone wemod-launcher into ~/wemod-launcher "
                               "and open the setup tutorial");
        }
    }

    // --- Status -------------------------------------------------------
    // One line under the actions: what is running, how the last run
    // ended, or the initial hint. Detail lives in the log below.
    ImGui::Spacing();
    if (state.running) {
        text_disabled(running_status(state.kind));
    } else if (state.has_run) {
        if (state.last_exit_code == 0) {
            text_colored(color_ok, "Done. Launch WeMod - Pro is active.");
        } else {
            text_colored(color_err, std::format("Failed (exit code {})",
                                                state.last_exit_code));
        }
    } else {
        text_disabled("Patch, then launch WeMod.");
    }

    // --- Settings (collapsed by default), full window width -----------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Settings")) {
        ImGui::Indent(section_indent);
        draw_settings(state, script_ok);
        ImGui::Unindent();
    }

    // --- Log: fills the rest of the window ----------------------------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float line_height{ImGui::GetTextLineHeightWithSpacing()};
    const float toolbar_h{row_h + line_height + (style.ItemSpacing.y * 3.0F)};
    const float log_height{std::max(ImGui::GetContentRegionAvail().y - toolbar_h,
                                    line_height * 4.0F)};

    ImGui::BeginChild("##log", ImVec2(0.0F, log_height),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (state.log.empty()) {
        text_disabled(
            "Patch output appears here. Copy it with the button below "
            "when something fails.");
    } else {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextUnformatted(state.log.data(),
                               state.log.data() + state.log.size());
        ImGui::PopTextWrapPos();
    }
    if (state.scroll_to_bottom) {
        ImGui::SetScrollHereY(1.0F);
        state.scroll_to_bottom = false;
    }
    ImGui::EndChild();

    // --- Bottom toolbar: Copy / Clear / Report, equal-width -----------
    ImGui::Spacing();
    const float util_w{equal_button_width(3)};
    if (action_button("Copy output", util_w, row_h)) {
        copy_output(state);
    }
    if (ImGui::IsItemHovered()) {
        tooltip_text("Copy the log and environment info to the clipboard");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.log.empty());
    if (action_button("Clear output", util_w, row_h)) {
        clear_output(state);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip_text("Clear the log");
    }
    ImGui::SameLine();
    if (action_button("Report bug", util_w, row_h)) {
        report_bug(state);
    }
    if (ImGui::IsItemHovered()) {
        tooltip_text("Open a pre-filled GitHub issue with the log attached");
    }

    // Footer under the full-span row: Copied! left, version centered.
    ImGui::Spacing();
    const float footer_y{ImGui::GetCursorPosY()};
    if (state.copied_flash > 0.0F) {
        state.copied_flash -= ImGui::GetIO().DeltaTime;
        text_colored(color_ok, "Copied!");
    }
    {
        const std::string version_text{std::format("v{}", gui_version)};
        const float text_width{ImGui::CalcTextSize(version_text.c_str()).x};
        const float content_min{ImGui::GetWindowContentRegionMin().x};
        const float content_max{ImGui::GetWindowContentRegionMax().x};
        const float content_span{content_max - content_min};
        const float version_x{content_min +
                              ((content_span - text_width) * 0.5F)};
        ImGui::SetCursorPos(ImVec2(version_x, footer_y));
        text_disabled(version_text);
        if (ImGui::IsItemHovered()) {
            tooltip_text(std::string(SDL_GetPlatform()) + " " +
                         std::string(target_arch));
        }
    }

    ImGui::End();
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    Expects(appstate != nullptr);
    (void)argc;
    (void)argv;

    // SDL_AppQuit runs even when SDL_AppInit fails and calls SDL_Quit
    // there - no cleanup needed in this scope.
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        sdl_log_error(std::string("SDL_Init: ") + SDL_GetError());
        return SDL_APP_FAILURE;
    }

    const auto [win_w, win_h]{pick_window_size()};

    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    if (!SDL_CreateWindowAndRenderer(
            "WeMod Enhancer", win_w, win_h,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window,
            &renderer)) {
        sdl_log_error(std::string("SDL_CreateWindowAndRenderer: ") +
                      SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetWindowMinimumSize(window, window_min_width, window_min_height);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Density only: default dark colors stay. Opened padding matches
    // the settings-page habit (generous hit targets, even gaps) so
    // FHD and 4K both read as the same layout, just larger.
    ImGuiStyle& style{ImGui::GetStyle()};
    style.WindowPadding = ImVec2(16.0F, 14.0F);
    style.FramePadding = ImVec2(14.0F, 8.0F);
    style.ItemSpacing = ImVec2(10.0F, 8.0F);
    style.ItemInnerSpacing = ImVec2(8.0F, 6.0F);
    style.ScrollbarSize = 16.0F;
    style.GrabMinSize = 14.0F;

    // One knob scales the whole UI: style.FontScaleDpi (ImGui 1.92+)
    // scales every font-size-derived widget, and the window was
    // created at logical size so SDL keeps the physical size constant
    // across DPIs. SDL returns 0.0 when the scale is unknown - that
    // would zero every widget, so fall back to 1.0.
    const float main_scale{SDL_GetDisplayContentScale(
        SDL_GetPrimaryDisplay())};
    style.FontScaleDpi = main_scale > 0.0F ? main_scale : 1.0F;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    auto state_owner{std::make_unique<app_state>()};
    app_state& state{*state_owner};
    state.window = window;
    state.renderer = renderer;
    state.install_dir = default_install_dir();
    state.script_path = bundled_script().string();
    state.python = std::string(default_python);

    // Say what was auto-detected up front - the log doubles as the
    // diagnostics report.
    if (const fs::path app{resolve_wemod_dir(state.install_dir)};
        !app.empty()) {
        state.install_dir = app.string();
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
                       "\n  fix: re-download the GUI package from the GitHub "
                       "releases and unpack the whole folder - it is "
                       "self-contained.\n\n");
    }
    start_probe(state);

    *appstate = state_owner.release();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    Expects(event != nullptr);
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    (void)appstate;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    app_state& state{state_of(appstate)};

    poll_run(state);

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    draw_ui(state);

    ImGui::Render();
    SDL_SetRenderDrawColorFloat(state.renderer, clear_color.x,
                                clear_color.y, clear_color.z,
                                clear_color.w);
    SDL_RenderClear(state.renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),
                                          state.renderer);
    SDL_RenderPresent(state.renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    (void)result;
    // Re-acquire ownership: the unique_ptr destroys app_state here,
    // after the backends are shut down. No manual delete anywhere.
    const std::unique_ptr<app_state> state_owner{
        static_cast<app_state*>(appstate)};

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (state_owner) {
        SDL_DestroyRenderer(state_owner->renderer);
        SDL_DestroyWindow(state_owner->window);
    }
    SDL_Quit();
}
