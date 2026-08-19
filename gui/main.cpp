// gui/main.cpp
//
// Dear ImGui (SDL3 + SDL_Renderer) frontend for the Python patcher
// CLI. Runs `patch` / `restore` for users who are not comfortable with
// a terminal and shows stdout/stderr live, plus the exit code, in a
// scrolling log.
//
// The patcher itself always comes from the GitHub releases: on first
// run the app downloads the latest asset (curl on Linux, PowerShell on
// Windows), unpacks wemod_enhancer.py + version.dll into the SDL pref
// dir and reuses that copy from then on. No local files, no build-
// machine paths baked in - the exe can be moved between PCs as-is.
// The only thing the user provides is the WeMod folder (auto-detected
// at startup, so usually that is just pressing Patch).
//
// Structure: SDL3 app callbacks (SDL_MAIN_USE_CALLBACKS is set via
// target_compile_definitions in gui/CMakeLists.txt, keeping this file
// macro-free) - SDL_AppInit / SDL_AppIterate / SDL_AppEvent /
// SDL_AppQuit. Modern C++23 notes:
//   - `if constexpr` on the compile-time SDL platform tag replaces
//     #ifdef wherever both branches compile; the preprocessor only
//     remains where names do not exist cross-platform (popen/pclose).
//   - SDL3 owns the platform glue: SDL_GetEnvironmentVariable instead
//     of std::getenv, SDL_GetPrefPath instead of hand-rolled cache-dir
//     logic, SDL_GetPlatform for diagnostics, RAII for SDL_malloc'd
//     strings.
//   - gsl::not_null for pointers out of C callbacks, gsl::finally for
//     SDL_Quit, Expects() for preconditions; app state ownership is a
//     std::unique_ptr (make_unique in SDL_AppInit, re-acquired in
//     SDL_AppQuit) - no raw new/delete anywhere.
//   - std::async + std::future drives the worker thread: popen() has
//     no cancellation point, so a std::jthread stop_token would be
//     dead weight - SDL_AppIterate just polls wait_for(0) per frame.
//   - ImGui's text API is printf-varargs only; the text_unformatted /
//     text_colored / text_disabled / tooltip_text helpers below keep
//     every call site vararg-free (cppcoreguidelines-pro-type-vararg).
//   - "Report bug" opens a pre-filled GitHub issue (env info + log as
//     markdown) via SDL_OpenURL - a one-click bug report.
//   - Explicit-width integer types (std::int32_t and friends) wherever
//     a width is actually relied on; plain int only at C-API
//     boundaries that demand it (fgets, pclose, SDL_CreateWindow).
//   - Structs are `final`: no inheritance, no vtable surprises, and
//     default-constructible members carry no redundant {} initializer
//     (readability-redundant-member-init) - only non-empty defaults
//     are spelled out.
//   - window_title is a plain `const char*` (a string literal is
//     always null-terminated), so SDL/ImGui C APIs take it directly -
//     no string_view::data() that may not be null-terminated
//     (bugprone-suspicious-stringview-data-usage).
//   - No hand-rolled UI scale factor: the display content scale from
//     SDL (main_scale) is the one correct multiplier - it tracks the
//     monitor's DPI, so the UI looks identical on every display.
//   - Folder validity is an INVARIANT of the current field text:
//     resolve_wemod_dir() runs every frame and accepts both the
//     app-x.y.z directory and the WeMod root above it (resolved to the
//     newest app-* inside), so the field can never look "ok" while
//     Patch refuses to run.
//   - The log shows the REAL command line for every background job
//     (git clone ..., curl ..., python ...) - no shorthand, no "->",
//     so the user sees exactly what ran and can reproduce it.
//
// NOTE: imgui's default font covers ASCII only - keep every literal in
// this file plain ASCII (no em-dashes, arrows or ellipsis characters).

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_stdlib.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

// SDL_MAIN_USE_CALLBACKS comes from the build system (see
// gui/CMakeLists.txt) - no #define needed here.
#include <SDL3/SDL_main.h>

#include <gsl/gsl> // gsl::not_null, gsl::finally, gsl::at, Expects

#include <algorithm>
#include <array>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h> // WIFEXITED / WEXITSTATUS for pclose()
#endif

namespace fs = std::filesystem;

namespace {

// Compile-time platform tag from SDL3's platform defines.
#ifdef SDL_PLATFORM_WINDOWS
constexpr bool is_windows{true};
#else
constexpr bool is_windows{false};
#endif

// Compile-time target architecture, for the diagnostics report.
#if defined(__x86_64__) || defined(_M_X64)
constexpr std::string_view target_arch{"amd64"};
#elif defined(__aarch64__) || defined(_M_ARM64)
constexpr std::string_view target_arch{"arm64"};
#else
constexpr std::string_view target_arch{"unknown"};
#endif

// Latest GitHub release asset carrying the patcher (wemod_enhancer.py
// + version.dll under bin/). Downloaded on first run - the only remote
// endpoint the app knows; everything local is derived at runtime.
constexpr std::string_view release_asset_url{
    is_windows
        ? std::string_view(
              "https://github.com/e-gleba/wemod_enhancer/releases/latest/download/"
              "wemod_enhancer-windows-msvc-amd64.zip")
        : std::string_view(
              "https://github.com/e-gleba/wemod_enhancer/releases/latest/download/"
              "wemod_enhancer-windows-llvm-mingw-amd64.tar.xz")};

// "Report bug" target: a pre-filled GitHub issue. The body rides in
// the URL query, so the log is capped to keep the URL portable across
// browsers and proxies.
constexpr std::string_view issue_new_url{
    "https://github.com/e-gleba/wemod_enhancer/issues/new"};
constexpr std::size_t issue_log_budget{3000};

constexpr std::string_view default_python{
    is_windows ? std::string_view("python") : std::string_view("python3")};

// A string literal is always null-terminated, so SDL/ImGui C APIs take
// it directly - no string_view::data() that may not be null-terminated.
constexpr const char* window_title{"WeMod Enhancer"};

// Base window size, multiplied by the display DPI scale at startup.
constexpr std::int32_t window_width{1024};
constexpr std::int32_t window_height{640};
static_assert(window_width > 0 && window_height > 0,
              "window size must be positive");

// ImVec4 has a user-provided constructor (not an aggregate), so
// designated initializers do not apply - brace-init it is.
constexpr ImVec4 clear_color{0.10F, 0.10F, 0.12F, 1.00F};

// Status colors (replacing the magic-number literals).
constexpr ImVec4 color_ok{0.35F, 0.85F, 0.45F, 1.00F};
constexpr ImVec4 color_err{0.90F, 0.30F, 0.30F, 1.00F};
constexpr ImVec4 color_warn{0.90F, 0.60F, 0.20F, 1.00F};

// Input field tint when the path is valid: quiet green fill.
constexpr ImVec4 field_ok_bg{0.14F, 0.32F, 0.16F, 0.70F};

struct run_result final
{
    std::int32_t exit_code;
    std::string output;
};

// What the current background command is, so poll_run() can react to
// completion: refresh resolved paths after a download, record the
// Python probe result, re-check the wemod-launcher clone...
enum class run_kind : std::uint8_t { patcher, bootstrap, probe, launcher };
static_assert(std::is_enum_v<run_kind>);

// Python probe tri-state: unknown / failed / works.
enum class probe_state : std::uint8_t { unknown, failed, works };

// Default-constructible members (string, future) carry no redundant {}
// initializer - they default-construct empty on their own. Only
// non-empty defaults are spelled out.
struct app_state final
{
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    std::string install_dir;
    std::string script_path;
    std::string python;
    std::string version_dll; // empty = the script auto-detects it
    std::string log;
    std::future<run_result> pending;
    bool running{false};
    run_kind kind{run_kind::patcher};
    bool scroll_to_bottom{false};
    bool has_run{false};
    std::int32_t last_exit_code{0};
    float copied_flash{0.0F}; // seconds left of "Copied!" feedback
    // --- diagnostics ---
    probe_state python_ok{probe_state::unknown};
    std::string python_version;  // e.g. "Python 3.13.5"
    std::string python_location; // e.g. /usr/bin/python3
    bool launcher_present{false}; // Linux: ~/wemod-launcher exists
    bool launcher_note{false};    // "I've done it" but still missing
};

// RAII for SDL_malloc'd strings (SDL_GetBasePath, SDL_GetPrefPath).
struct sdl_free final
{
    void operator()(void* ptr) const noexcept { SDL_free(ptr); }
};
using sdl_string = std::unique_ptr<char, sdl_free>;

// --- Vararg-free ImGui helpers --------------------------------------
// ImGui's formatted text API is printf-style varargs; these wrappers
// keep every call site clean (cppcoreguidelines-pro-type-vararg).

void text_unformatted(const std::string_view text)
{
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
}

void text_colored(const ImVec4& color, const std::string_view text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    text_unformatted(text);
    ImGui::PopStyleColor();
}

void text_disabled(const std::string_view text)
{
    text_colored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), text);
}

// Same wrapping width ImGui's SetTooltip uses, minus the varargs.
void tooltip_text(const std::string_view text)
{
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0F);
        text_unformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// SDL_Log is varargs-only; one suppression here beats one per call site.
void sdl_log_error(const std::string& message)
{
    SDL_Log("%s", message.c_str()); // NOLINT(cppcoreguidelines-pro-type-vararg)
}

// Process environment via SDL3 (no std::getenv, no CRT quirks).
[[nodiscard]] const char* env_var(const char* name) noexcept
{
    return SDL_GetEnvironmentVariable(SDL_GetEnvironment(), name);
}

// Report a fatal startup error; SDL_ShowSimpleMessageBox may be called
// before SDL_Init, so this works for every early failure - and GUI
// users actually see it.
[[nodiscard]] SDL_AppResult fatal_error(const std::string& what)
{
    const std::string message{what + ": " + SDL_GetError()};
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             window_title,
                             message.c_str(),
                             nullptr);
    return SDL_APP_FAILURE;
}

// Quote one argument for the platform shell behind popen().
[[nodiscard]] constexpr std::string shell_quote(const std::string_view arg)
{
    if constexpr (is_windows) {
        return "\"" + std::string(arg) + "\"";
    } else {
        std::string quoted{"'"};
        for (const char c : arg) {
            if (c == '\'') {
                quoted += "'\\''";
            } else {
                quoted += c;
            }
        }
        return quoted + "'";
    }
}

// Run the command, reading its stdout+stderr line by line as they are
// produced (popen), so the log streams live instead of appearing only
// after the process exits. Blocking - call from a worker thread.
[[nodiscard]] run_result run_command(const std::string& command)
{
    // 2>&1: child stderr lands in the same live stream as stdout.
    // popen/pclose carry underscore names in the MSVC CRT - the one
    // place where #ifdef is unavoidable.
#ifdef _WIN32
    FILE* pipe{_popen((command + " 2>&1").c_str(), "r")};
#else
    FILE* pipe{popen((command + " 2>&1").c_str(), "r")};
#endif

    run_result result{.exit_code = -1, .output = {}};
    if (pipe == nullptr) {
        result.output = "error: could not start the command\n";
        return result;
    }

    std::string pending_line;
    std::array<char, 512> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
           nullptr) {
        pending_line += buffer.data();
        // Flush on newline so the log grows visibly while the patcher
        // runs instead of arriving as one blob at the end.
        if (pending_line.find('\n') != std::string::npos) {
            result.output += pending_line;
            pending_line.clear();
        }
    }
    result.output += pending_line; // trailing partial line

#ifdef _WIN32
    result.exit_code = _pclose(pipe); // already the process exit code
#else
    const int status{pclose(pipe)};
    if (status != -1 && WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }
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
        const std::string token{
            name.substr(pos, dot == std::string::npos ? dot : dot - pos)};
        std::int32_t value{0};
        std::from_chars(token.data(), token.data() + token.size(), value);
        parts.push_back(value);
        if (dot == std::string::npos) {
            break;
        }
        pos = dot + 1;
    }
    return parts;
}

// Newest app-* directory under root that contains resources/app.asar
// (the layout the patcher expects as --install-dir).
[[nodiscard]] fs::path find_app_dir(const fs::path& root)
{
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return {};
    }
    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec) ||
            !entry.path().filename().string().starts_with("app-") ||
            !fs::is_regular_file(entry.path() / "resources" / "app.asar",
                                 ec)) {
            continue;
        }
        candidates.push_back(entry.path());
    }
    const auto newest{
        std::ranges::max_element(candidates, {}, [](const fs::path& path) {
            return version_parts(path.filename().string());
        })};
    return newest == candidates.end() ? fs::path{} : *newest;
}

// Pre-gathered install dir: when this hits, the user never opens the
// Browse dialog - the path is already filled in and Patch is ready.
[[nodiscard]] std::string default_install_dir()
{
    if constexpr (is_windows) {
        // Mirror the readme PowerShell snippet: newest
        // %LOCALAPPDATA%\WeMod\app-* that has resources\app.asar.
        if (const char* local{env_var("LOCALAPPDATA")}) {
            const fs::path wemod{fs::path(local) / "WeMod"};
            if (const fs::path app{find_app_dir(wemod)}; !app.empty()) {
                return app.string();
            }
            return wemod.string();
        }
    } else {
        // wemod-launcher layout (see readme "Linux / Steam Deck").
        if (const char* home{env_var("HOME")}) {
            return (fs::path(home) / "wemod-launcher" / "wemod_data" /
                    "wemod_bin")
                .string();
        }
    }
    return {};
}

// Where the Browse... dialog opens: the usual install location, so the
// user is one click away instead of navigating from the filesystem root.
[[nodiscard]] std::string browse_root()
{
    if constexpr (is_windows) {
        if (const char* local{env_var("LOCALAPPDATA")}) {
            return (fs::path(local) / "WeMod").string();
        }
    } else {
        if (const char* home{env_var("HOME")}) {
            const fs::path launcher{fs::path(home) / "wemod-launcher"};
            std::error_code ec;
            if (fs::is_directory(launcher, ec)) {
                return launcher.string();
            }
            return {home};
        }
    }
    return {};
}

// Resolve whatever the user picked to the directory the patcher needs:
// the app-x.y.z folder holding resources/app.asar. Accepts that folder
// directly OR the WeMod root above it (the newest app-* inside wins) -
// both are what a Browse dialog naturally returns. This is the single
// validity invariant: it runs every frame on the current field text,
// so the field can never look valid while Patch refuses to run.
[[nodiscard]] fs::path resolve_wemod_dir(const std::string& dir)
{
    if (dir.empty()) {
        return {};
    }
    std::error_code ec;
    if (fs::is_regular_file(fs::path(dir) / "resources" / "app.asar", ec)) {
        return fs::path(dir);
    }
    return find_app_dir(dir); // WeMod root -> newest app-*, else {}
}

// SDL dialog callback: may run on another thread; it only writes a
// std::string that the UI thread reads next frame - safe in practice
// because the dialog is modal and the field is not edited meanwhile.
void SDLCALL on_folder_chosen(void* userdata,
                              const char* const* filelist,
                              int filter)
{
    (void)filter;
    const gsl::not_null target{static_cast<std::string*>(userdata)};
    if (filelist != nullptr && *filelist != nullptr) {
        *target = *filelist;
    }
}

// Per-user writable dir for the downloaded patcher. SDL_GetPrefPath
// picks the right place on every OS (%APPDATA%\org\app on Windows,
// XDG data home on Linux) and creates the directory for us.
[[nodiscard]] fs::path bootstrap_root()
{
    if (const sdl_string pref{SDL_GetPrefPath("e-gleba", "wemod_enhancer")}) {
        return {pref.get()};
    }
    return fs::temp_directory_path() / "wemod_enhancer";
}

// The release archive carries the CLI under bin/; search recursively
// so a layout tweak does not silently break the unpack step.
[[nodiscard]] fs::path find_script_under(const fs::path& root)
{
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
        if (entry.is_regular_file(ec) &&
            entry.path().filename() == "wemod_enhancer.py") {
            return entry.path();
        }
    }
    return {};
}

// The patcher always comes from the GitHub releases: downloaded once
// into the pref dir, then reused on every launch. Settings has a
// button to re-download the newest release.
[[nodiscard]] std::string cached_script()
{
    return find_script_under(bootstrap_root() / "patcher").string();
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
    state.log += "$ " + shown + "\n";
    state.kind = kind;
    state.running = true;
    if (kind == run_kind::patcher) {
        state.has_run = true;
    }
    state.scroll_to_bottom = true;
    state.pending = std::async(std::launch::async, run_command, command);
}

void start_run(app_state& state, const char* subcommand)
{
    // python -u: unbuffered stdout/stderr. Python block-buffers pipes
    // by default, which would make the "live" log arrive in 4 KiB
    // chunks - with -u every print lands immediately.
    std::string shown{state.python + " -u " + state.script_path + " " +
                      subcommand + " --install-dir " + state.install_dir};
    std::string command{shell_quote(state.python) + " -u " +
                        shell_quote(state.script_path) + " " + subcommand +
                        " --install-dir " + shell_quote(state.install_dir)};
    if (!state.version_dll.empty() &&
        std::string_view(subcommand) == "patch") {
        shown += " --version-dll " + state.version_dll;
        command += " --version-dll " + shell_quote(state.version_dll);
    }
    start_command(state, run_kind::patcher, shown, command);
}

// Download + unpack the latest release asset with the tools every base
// OS install already has: curl + tar on Linux, PowerShell on Windows.
// The log shows the real command line - no shorthand.
void start_bootstrap(app_state& state)
{
    const fs::path dir{bootstrap_root() / "patcher"};
    std::string command;
    if constexpr (is_windows) {
        const fs::path archive{bootstrap_root() / "patcher.zip"};
        command =
            "powershell -NoProfile -ExecutionPolicy Bypass -Command \""
            "$ProgressPreference='SilentlyContinue'; "
            "New-Item -ItemType Directory -Force -Path '" + dir.string() +
            "' | Out-Null; "
            "Invoke-WebRequest -Uri '" + std::string(release_asset_url) +
            "' -OutFile '" + archive.string() + "'; "
            "Expand-Archive -Force -Path '" + archive.string() +
            "' -DestinationPath '" + dir.string() + "'\"";
    } else {
        const fs::path archive{bootstrap_root() / "patcher.tar.xz"};
        command =
            "mkdir -p " + shell_quote(dir.string()) + " && curl -fL " +
            shell_quote(release_asset_url) + " -o " +
            shell_quote(archive.string()) + " && tar -xf " +
            shell_quote(archive.string()) + " -C " +
            shell_quote(dir.string());
    }
    start_command(state, run_kind::bootstrap, command, command);
}

// One-shot interpreter check: prints the version (kept as the classic
// `$ python --version` line in the log) and where the binary lives;
// the Settings section shows both.
void start_probe(app_state& state)
{
    const std::string command{
        shell_quote(state.python) + " --version && " +
        (is_windows ? "where " + state.python
                    : "command -v " + shell_quote(state.python))};
    start_command(state,
                  run_kind::probe,
                  state.python + " --version",
                  command);
}

// Probe output is the `--version` line followed by the executable
// location (where / command -v). popen on Windows hands us CRLF.
void parse_probe(app_state& state, const std::string& output)
{
    state.python_version.clear();
    state.python_location.clear();
    std::size_t pos{0};
    while (pos < output.size()) {
        const std::size_t eol{output.find('\n', pos)};
        std::string_view line{
            output.data() + pos,
            (eol == std::string::npos ? output.size() : eol) - pos};
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.starts_with("Python ")) {
            state.python_version = std::string(line);
        } else if (!line.empty() && state.python_location.empty()) {
            state.python_location = std::string(line);
        }
        if (eol == std::string::npos) {
            break;
        }
        pos = eol + 1;
    }
}

// wemod-launcher clone location used by the readme tutorial. Linux-only
// in practice (HOME is unset on Windows, so the row stays hidden).
[[nodiscard]] fs::path launcher_dir()
{
    if (const char* home{env_var("HOME")}) {
        return fs::path(home) / "wemod-launcher";
    }
    return {};
}

[[nodiscard]] bool launcher_installed()
{
    std::error_code ec;
    const fs::path dir{launcher_dir()};
    return !dir.empty() && fs::is_directory(dir, ec);
}

// Same steps as the readme tutorial: clone into ~, mark the launcher
// script executable. The log shows the real command line - no shorthand.
void start_launcher_clone(app_state& state)
{
    const fs::path dir{launcher_dir()};
    const std::string command{
        "git clone https://github.com/DaniAsh551/wemod-launcher " +
        shell_quote(dir.string()) + " && chmod +x " +
        shell_quote((dir / "wemod").string())};
    start_command(state, run_kind::launcher, command, command);
}

void poll_run(app_state& state)
{
    if (!state.running ||
        state.pending.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
        return;
    }
    const run_result result{state.pending.get()};
    state.log += result.output;
    if (!result.output.empty() && !result.output.ends_with('\n')) {
        state.log += '\n';
    }
    state.log += "[exit code: " + std::to_string(result.exit_code) +
        "]\n\n";
    state.last_exit_code = result.exit_code;
    state.running = false;
    state.scroll_to_bottom = true;

    // Every failure says what happened and how to fix it - the log is
    // the error report (see the Copy output / Report bug buttons).
    switch (state.kind) {
    case run_kind::bootstrap:
        if (result.exit_code != 0) {
            state.log += is_windows
                ? "error: could not download the patcher.\n"
                  "  fix: check the network connection and that powershell "
                  "is available, then use Download now below.\n\n"
                : "error: could not download the patcher.\n"
                  "  fix: check the network connection and that curl is "
                  "installed, then use Download now below.\n\n";
            break;
        }
        if (state.script_path = cached_script(); !state.script_path.empty()) {
            state.log += "patcher ready: " + state.script_path + "\n\n";
            start_probe(state); // chain: confirm Python works too
        } else {
            state.log += "error: release downloaded but wemod_enhancer.py "
                         "was not inside.\n"
                         "  fix: press Report bug below - this should not "
                         "happen.\n\n";
        }
        break;
    case run_kind::probe:
        state.python_ok = result.exit_code == 0 ? probe_state::works
                                                : probe_state::failed;
        parse_probe(state, result.output);
        break;
    case run_kind::launcher:
        state.launcher_present = launcher_installed();
        if (!state.launcher_present && result.exit_code != 0) {
            state.log += "error: could not clone wemod-launcher.\n"
                         "  fix: make sure git is installed (preinstalled "
                         "on SteamOS), then retry.\n\n";
        }
        break;
    case run_kind::patcher:
        if (result.exit_code != 0) {
            state.log += "hint: close WeMod fully, then retry. If it still "
                         "fails, press Report bug below - the issue opens "
                         "pre-filled with this log.\n\n";
        }
        break;
    }
}

// Environment block prepended to bug reports (clipboard and GitHub
// issue alike), so a shared log carries the locations and versions
// needed to reproduce it.
[[nodiscard]] std::string env_info(const app_state& state)
{
    std::string info{"wemod_enhancer gui\n"};
    info += "platform: " + std::string(SDL_GetPlatform()) + " " +
        std::string(target_arch) + "\n";
    info += "wemod folder: " +
        (state.install_dir.empty() ? std::string("<not set>")
                                   : state.install_dir) +
        "\n";
    info += "patcher script: " +
        (state.script_path.empty() ? std::string("<not downloaded yet>")
                                   : state.script_path) +
        "\n";
    info += "python command: " + state.python + "\n";
    if (!state.python_version.empty()) {
        info += "python version: " + state.python_version + "\n";
    }
    if (!state.python_location.empty()) {
        info += "python location: " + state.python_location + "\n";
    }
    info += "\n";
    return info;
}

// Bounds-checked nibble -> hex digit. gsl::at keeps the index checked,
// so no raw array subscript ever appears here
// (cppcoreguidelines-pro-bounds-constant-array-index).
[[nodiscard]] constexpr char hex_digit(const std::uint8_t nibble) noexcept
{
    constexpr std::string_view digits{"0123456789ABCDEF"};
    static_assert(digits.size() == 16);
    return gsl::at(digits, nibble);
}

// RFC 3986 percent-encoding (unreserved characters pass through), for
// the pre-filled GitHub issue URL behind the Report bug button.
[[nodiscard]] std::string url_encode(const std::string_view text)
{
    constexpr auto is_unreserved = [](const unsigned char c) constexpr {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~';
    };
    static_assert(is_unreserved('a') && !is_unreserved(' '),
                  "RFC 3986 unreserved set");

    std::string encoded;
    encoded.reserve(text.size());
    for (const unsigned char c : text) {
        if (is_unreserved(c)) {
            encoded += static_cast<char>(c);
        } else {
            encoded += '%';
            encoded += hex_digit(static_cast<std::uint8_t>(c >> 4U));
            encoded += hex_digit(static_cast<std::uint8_t>(c & 0x0FU));
        }
    }
    return encoded;
}

// Pre-filled issue URL: env info + the tail of the log as markdown
// (fenced code blocks), capped so the URL stays portable - the full
// log is always one "Copy output" away.
[[nodiscard]] std::string bug_report_url(const app_state& state)
{
    std::string body{"## Environment\n\n```text\n" + env_info(state) +
                     "```\n\n## Log\n\n```text\n"};
    std::string_view log{state.log};
    if (log.size() > issue_log_budget) {
        body += "[... truncated - full log via \"Copy output\" ...]\n";
        log = log.substr(log.size() - issue_log_budget);
    }
    body += log;
    body += "\n```\n";
    return std::string(issue_new_url) +
        "?title=" + url_encode("bug: gui report") +
        "&body=" + url_encode(body);
}

// Button that fits its label (no hardcoded width, so the text never
// overflows). Returns true when clicked.
bool fit_button(const char* label)
{
    const ImGuiStyle& style{ImGui::GetStyle()};
    const float width{
        ImGui::CalcTextSize(label).x + (style.FramePadding.x * 2.0F)};
    return ImGui::Button(label, ImVec2{width, 0.0F});
}

// Why an action button is disabled, or what it does when it is not -
// no nested conditional operators, just early returns.
[[nodiscard]] const char* action_tooltip(const bool install_ok,
                                         const bool script_ok,
                                         const char* ready) noexcept
{
    if (!install_ok) {
        return "Pick a valid WeMod folder first";
    }
    if (!script_ok) {
        return "Waiting for the patcher - see Settings";
    }
    return ready;
}

// Status line shown while a background command runs.
[[nodiscard]] std::string_view running_status(const run_kind kind) noexcept
{
    switch (kind) {
    case run_kind::bootstrap:
        return "Downloading the latest patcher release...";
    case run_kind::probe:
        return "Checking Python...";
    case run_kind::launcher:
        return "Downloading wemod-launcher...";
    case run_kind::patcher:
        break;
    }
    return "Running - keep WeMod closed...";
}

// Small "(?)" marker: hover shows a short description, click opens the
// docs link in the system browser. Used sparingly, only where a new
// user genuinely needs a pointer.
void help_marker(const char* description, const char* url)
{
    ImGui::SameLine();
    text_disabled("(?)");
    if (ImGui::IsItemHovered()) {
        tooltip_text(std::string(description) + "\n\nClick to open: " + url);
    }
    if (ImGui::IsItemClicked() && !SDL_OpenURL(url)) {
        sdl_log_error(std::string("SDL_OpenURL(") + url +
                      "): " + SDL_GetError());
    }
}

// One labeled row: label on the left, content to its right.
void field_label(const char* label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
}

void draw_ui(app_state& state)
{
    poll_run(state);

    const ImGuiIO& io{ImGui::GetIO()};
    ImGui::SetNextWindowPos(ImVec2{0.0F, 0.0F});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##wemod_enhancer",
                 nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    // Clean single-column layout: the one required input, the two
    // actions, a collapsed Settings section, then the log. The window
    // title bar already says "WeMod Enhancer" - no duplicate title here.

    // --- WeMod folder: the only thing a user must provide -----------
    // Validity is an invariant of the current field text, recomputed
    // every frame: the app-x.y.z dir itself OR the WeMod root above it
    // (resolved to the newest app-* inside). Typing or deleting text
    // re-validates immediately; Patch normalizes the field to the
    // resolved dir so the log always shows the exact folder used.
    const fs::path resolved_dir{resolve_wemod_dir(state.install_dir)};
    const bool install_ok{!resolved_dir.empty()};
    const bool script_ok{!state.script_path.empty() &&
                         fs::is_regular_file(state.script_path)};

    field_label("WeMod folder");
    help_marker(
        "Folder where WeMod is installed - the app-x.y.z directory that "
        "contains resources\\app.asar, or the WeMod root above it (the "
        "newest app-* inside is used automatically). Auto-detected at "
        "startup when possible.",
        "https://github.com/e-gleba/wemod_enhancer#wemod-enhancer");
    const float browse_w{ImGui::CalcTextSize("Browse...").x +
                         (ImGui::GetStyle().FramePadding.x * 2.0F)};
    ImGui::SetNextItemWidth(-browse_w - ImGui::GetStyle().ItemSpacing.x);
    // The field itself is the validity indicator, no extra text:
    // valid path -> quiet green fill; invalid path -> red border only;
    // empty -> plain.
    if (install_ok) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, field_ok_bg);
    } else if (!state.install_dir.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Border, color_err);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
    }
    constexpr const char* install_hint{
        is_windows ? R"(C:\Users\<you>\AppData\Local\WeMod\app-10.x.x)"
                   : "~/wemod-launcher/wemod_data/wemod_bin"};
    ImGui::InputTextWithHint("##install_dir", install_hint, &state.install_dir);
    if (install_ok) {
        ImGui::PopStyleColor();
    } else if (!state.install_dir.empty()) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (fit_button("Browse...")) {
        const std::string start_dir{[&state] {
            std::error_code ec;
            if (!state.install_dir.empty() &&
                fs::is_directory(state.install_dir, ec)) {
                return state.install_dir;
            }
            return browse_root();
        }()};
        SDL_ShowOpenFolderDialog(on_folder_chosen,
                                 &state.install_dir,
                                 state.window,
                                 start_dir.empty() ? nullptr
                                                   : start_dir.c_str(),
                                 false);
    }

    // Validity indicator: what the folder must contain, or what to do.
    if (install_ok) {
        text_colored(color_ok, "ready");
    } else if (!state.install_dir.empty()) {
        text_colored(color_err,
                     "must contain resources\\app.asar - download WeMod, "
                     "run it once, log in, then pick the folder again");
    } else {
        text_disabled(
            "must contain resources\\app.asar - download WeMod, run it "
            "once, log in, then pick the folder");
    }

    ImGui::Spacing();

    // --- Actions: Patch / Restore side by side, half width each -----
    const bool can_run{!state.running && install_ok && script_ok};
    const float button_width{(ImGui::GetContentRegionAvail().x -
                              ImGui::GetStyle().ItemSpacing.x) *
                             0.5F};
    ImGui::BeginDisabled(!can_run);
    if (ImGui::Button("Patch", ImVec2{button_width, 0.0F})) {
        // Normalize: the field and the log show the exact app-* dir
        // the patcher receives - no hidden indirection.
        state.install_dir = resolved_dir.string();
        start_run(state, "patch");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip_text(
            action_tooltip(install_ok, script_ok, "Unlock Pro features"));
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore", ImVec2{button_width, 0.0F})) {
        state.install_dir = resolved_dir.string();
        start_run(state, "restore");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip_text(action_tooltip(install_ok,
                                    script_ok,
                                    "Undo every change (uses backups)"));
    }
    ImGui::EndDisabled();
    if (state.running) {
        ImGui::SameLine();
        text_unformatted(running_status(state.kind));
    } else if (state.has_run && state.last_exit_code == 0) {
        ImGui::SameLine();
        text_colored(color_ok, "Done.");
    } else if (state.has_run) {
        ImGui::SameLine();
        text_colored(color_err,
                     "Failed (exit code " +
                         std::to_string(state.last_exit_code) + ")");
    }

    ImGui::Spacing();

    // --- Settings: merged diagnostics + advanced, collapsed by default
    if (ImGui::CollapsingHeader("Settings")) {
        ImGui::Indent();

        field_label("Python");
        help_marker(
            "Python 3.11+ runs the patcher. Preinstalled on SteamOS and "
            "most distros.",
            "https://www.python.org/downloads/");
        ImGui::SameLine();
        if (state.python_ok == probe_state::works) {
            text_colored(color_ok,
                         state.python_version.empty()
                             ? std::string_view("works")
                             : std::string_view(state.python_version));
            if (!state.python_location.empty()) {
                ImGui::SameLine();
                text_disabled(state.python_location);
            }
        } else if (state.python_ok == probe_state::failed) {
            text_colored(color_err,
                         "'" + state.python +
                             "' did not run - install Python 3.11+ or fix "
                             "the command below");
        } else {
            text_disabled("checking...");
        }

        field_label("Patcher");
        help_marker(
            "wemod_enhancer.py + version.dll, downloaded from the latest "
            "GitHub release into the app data dir. Download now fetches "
            "the newest release again.",
            "https://github.com/e-gleba/wemod_enhancer/releases/latest");
        ImGui::SameLine();
        if (script_ok) {
            text_colored(color_ok, "ready");
            ImGui::SameLine();
            text_disabled(state.script_path);
        } else if (state.running && state.kind == run_kind::bootstrap) {
            text_disabled("downloading the latest release...");
        } else {
            text_colored(color_err, "missing");
            ImGui::SameLine();
            ImGui::BeginDisabled(state.running);
            if (fit_button("Download now")) {
                start_bootstrap(state);
            }
            ImGui::EndDisabled();
        }

        // wemod-launcher (Linux / Steam Deck): the Proton wrapper from
        // the readme tutorial. Offer to fetch it the same way, or let
        // the user confirm it is already installed.
        if (const fs::path launcher{launcher_dir()}; !launcher.empty()) {
            field_label("wemod-launcher");
            help_marker(
                "Linux runs WeMod through wemod-launcher (Proton). The "
                "readme tutorial clones it into the home directory.",
                "https://github.com/DaniAsh551/wemod-launcher");
            ImGui::SameLine();
            if (state.launcher_present) {
                text_colored(color_ok, "found");
                ImGui::SameLine();
                text_disabled(launcher.string());
            } else {
                text_colored(color_warn, "not found in the home dir");
                ImGui::SameLine();
                ImGui::TextUnformatted(
                    "- download it like in the tutorial?");
                ImGui::SameLine();
                ImGui::BeginDisabled(state.running);
                if (fit_button("Yes, download")) {
                    start_launcher_clone(state);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (fit_button("I've done it")) {
                    state.launcher_present = launcher_installed();
                    state.launcher_note = !state.launcher_present;
                }
                if (state.launcher_note) {
                    text_colored(
                        color_warn,
                        "still not found at " + launcher.string() +
                            " - if it lives elsewhere, point the WeMod "
                            "folder above at it");
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        field_label("Python command");
        help_marker(
            "How Python 3.11+ is started on your system. Usually "
            "'python' on Windows, 'python3' on Linux.",
            "https://www.python.org/downloads/");
        ImGui::SetNextItemWidth(200.0F);
        constexpr const char* python_hint{is_windows ? "python" : "python3"};
        ImGui::InputTextWithHint("##python", python_hint, &state.python);

        field_label("Patcher script");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##script", &state.script_path);

        field_label("version.dll");
        help_marker(
            "The proxy DLL the patcher drops next to WeMod. Leave empty "
            "to use the copy downloaded next to wemod_enhancer.py.",
            "https://github.com/e-gleba/wemod_enhancer#wemod-enhancer");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##version_dll",
                                 "auto: next to the script",
                                 &state.version_dll);

        ImGui::Unindent();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Output");

    // --- Output: bounded child, toolbar pinned below it --------------
    const float toolbar_height{ImGui::GetFrameHeightWithSpacing() +
                               ImGui::GetStyle().ItemSpacing.y};
    ImGui::BeginChild("##log",
                      ImVec2{0.0F, -toolbar_height},
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (state.log.empty()) {
        text_disabled("Output of the patcher will appear here.");
    } else {
        ImGui::TextUnformatted(state.log.c_str());
    }
    if (state.scroll_to_bottom) {
        ImGui::SetScrollHereY(1.0F);
        state.scroll_to_bottom = false;
    }
    ImGui::EndChild();

    if (fit_button("Copy output")) {
        if (!state.log.empty()) {
            // Env info first: platform/arch, every resolved location
            // and the Python version - a self-contained bug report.
            const std::string report{env_info(state) + state.log};
            if (SDL_SetClipboardText(report.c_str())) {
                state.copied_flash = 1.5F;
            }
        }
    }
    if (ImGui::IsItemHovered()) {
        tooltip_text("Copy the environment info plus everything above - "
                     "paste it when asking for help");
    }
    ImGui::SameLine();
    if (fit_button("Report bug")) {
        // Pre-filled GitHub issue: env info + log tail as markdown.
        const std::string url{bug_report_url(state)};
        if (!SDL_OpenURL(url.c_str())) {
            sdl_log_error(std::string("SDL_OpenURL(issue): ") +
                          SDL_GetError());
        }
    }
    if (ImGui::IsItemHovered()) {
        tooltip_text("Open a pre-filled GitHub issue - environment info "
                     "and log attached as markdown");
    }
    ImGui::SameLine();
    if (fit_button("Clear")) {
        state.log.clear();
        state.has_run = false;
    }
    if (state.copied_flash > 0.0F) {
        state.copied_flash -= io.DeltaTime;
        ImGui::SameLine();
        text_colored(color_ok, "Copied!");
    }

    ImGui::End();
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return fatal_error("SDL_Init()");
    }

    // Window + SDL_Renderer (stock imgui SDL3+SDL_Renderer example).
    // The display content scale from SDL is the one correct multiplier:
    // it tracks the monitor DPI, so the UI keeps the same physical
    // size on every display. Guard it: SDL returns 0.0 when the scale
    // is unknown, which would size the window to nothing.
    const float main_scale{[] {
        const float scale{
            SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay())};
        return scale > 0.0F ? scale : 1.0F;
    }()};
    constexpr SDL_WindowFlags window_flags{SDL_WINDOW_RESIZABLE |
                                           SDL_WINDOW_HIDDEN |
                                           SDL_WINDOW_HIGH_PIXEL_DENSITY};
    SDL_Window* window{
        SDL_CreateWindow(window_title,
                         static_cast<int>(window_width * main_scale),
                         static_cast<int>(window_height * main_scale),
                         window_flags)};
    if (window == nullptr) {
        SDL_Quit();
        return fatal_error("SDL_CreateWindow()");
    }
    SDL_Renderer* renderer{SDL_CreateRenderer(window, nullptr)};
    if (renderer == nullptr) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return fatal_error("SDL_CreateRenderer()");
    }
    if (!SDL_SetRenderVSync(renderer, 1)) {
        // Non-fatal: worst case the UI redraws uncapped.
        sdl_log_error(std::string("SDL_SetRenderVSync: ") + SDL_GetError());
    }
    SDL_SetWindowPosition(window,
                          SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io{ImGui::GetIO()};
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGuiStyle& style{ImGui::GetStyle()};
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // make_unique + release: ownership moves to the SDL appstate void*
    // and is re-acquired by a unique_ptr in SDL_AppQuit - no raw
    // new/delete anywhere (cppcoreguidelines-owning-memory).
    auto state_owner{std::make_unique<app_state>()};
    app_state& state{*state_owner};
    state.window = window;
    state.renderer = renderer;
    state.install_dir = default_install_dir();
    state.script_path = cached_script();
    state.python = std::string(default_python);
    state.launcher_present = launcher_installed();

    // Say what was auto-detected up front - the log doubles as the
    // "what is happening" narration that bug reports share. The field
    // is normalized to the resolved app-* dir right away.
    if (const fs::path app{resolve_wemod_dir(state.install_dir)};
        !app.empty()) {
        state.install_dir = app.string();
        state.log =
            "auto-detected WeMod install: " + state.install_dir + "\n\n";
    }

    // The patcher always comes from the releases: reuse the unpacked
    // copy when present, otherwise download + unpack the latest asset.
    if (state.script_path.empty()) {
        start_bootstrap(state);
    } else {
        state.log += "using downloaded patcher: " + state.script_path + "\n\n";
        start_probe(state);
    }

    *appstate = state_owner.release();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    const gsl::not_null state_ptr{static_cast<app_state*>(appstate)};
    auto& state{*state_ptr};

    if ((SDL_GetWindowFlags(state.window) & SDL_WINDOW_MINIMIZED) != 0) {
        SDL_Delay(10);
        return SDL_APP_CONTINUE;
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    draw_ui(state);

    ImGui::Render();
    const ImGuiIO& io{ImGui::GetIO()};
    SDL_SetRenderScale(state.renderer,
                       io.DisplayFramebufferScale.x,
                       io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColorFloat(state.renderer,
                                clear_color.x,
                                clear_color.y,
                                clear_color.z,
                                clear_color.w);
    SDL_RenderClear(state.renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),
                                          state.renderer);
    SDL_RenderPresent(state.renderer);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    const gsl::not_null state_ptr{static_cast<app_state*>(appstate)};
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
        event->window.windowID == SDL_GetWindowID(state_ptr->window)) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    (void)result;
    // Runs even when SDL_AppInit failed halfway (appstate is null
    // then) - SDL_Quit() itself is always safe.
    const auto quit{gsl::finally([] { SDL_Quit(); })};
    if (appstate == nullptr) {
        return;
    }
    // Re-acquire ownership: the state is deleted automatically when
    // this goes out of scope - no explicit delete.
    const std::unique_ptr<app_state> state{
        static_cast<app_state*>(appstate)};

    // Wait for a running patch/restore so the pipe reader in the worker
    // thread finishes before the process exits.
    if (state->pending.valid()) {
        state->pending.wait();
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(state->renderer);
    SDL_DestroyWindow(state->window);
}
