// App logic: resolves dirs, launches background jobs, drains results.
// Talks to SDL only through platform.hpp. No ImGui here - ui.cpp owns
// every widget; this TU owns every decision.
#include "state.hpp"

#include "platform.hpp"

#include <gsl/assert>

#include <string>
#include <system_error>
#include <vector>

namespace wemod::gui
{

void append_log(app_state& state, const std::string_view text)
{
    state.log += text;
    if (state.log.size() > log_budget) {
        const std::size_t over{state.log.size() - log_budget};
        const std::size_t nl{state.log.find('\n', over)};
        state.log.erase(0, nl == std::string::npos ? over : nl + 1);
    }
}

fs::path resolve_wemod_dir(const std::string& dir)
{
    if (dir.empty()) {
        return {};
    }
    std::error_code ec;
    const fs::path picked{dir};
    // Direct hit: the app-x.y.z folder itself.
    if (fs::is_regular_file(picked / "resources" / "app.asar", ec)) {
        return picked;
    }
    // WeMod root: newest app-x.y.z inside (SDL-native scan).
    if (const fs::path app{newest_app_dir(picked)}; !app.empty()) {
        return app;
    }
    // wemod-launcher clone: wemod_data/wemod_bin inside (appears after
    // the first run + login - see the readme tutorial).
    ec.clear();
    const fs::path launcher_bin{picked / "wemod_data" / "wemod_bin"};
    if (fs::is_regular_file(launcher_bin / "resources" / "app.asar", ec)) {
        return launcher_bin;
    }
    return {};
}

void probe_filesystem(app_state& state)
{
    const auto now{std::chrono::steady_clock::now()};
    if (state.probed_install_dir == state.install_dir &&
        state.probed_script_path == state.script_path &&
        state.probed_version_dll == state.version_dll &&
        (now - state.last_probe) < reprobe_interval) {
        return;
    }
    state.probed_install_dir = state.install_dir;
    state.probed_script_path = state.script_path;
    state.probed_version_dll = state.version_dll;
    state.last_probe = now;
    state.resolved_install_dir = resolve_wemod_dir(state.install_dir);
    // error_code overload: a malformed path in the field must not throw.
    std::error_code ec;
    state.script_present = !state.script_path.empty() &&
        fs::is_regular_file(state.script_path, ec);
    ec.clear();
    state.dll_present = !state.version_dll.empty() &&
        fs::is_regular_file(state.version_dll, ec);
}

namespace
{

// `shown` mirrors the argv exactly (quoted for display) so the log stays
// reproducible while the child gets a clean argv - no shell involved.
[[nodiscard]] std::string format_argv(const std::vector<std::string>& argv)
{
    std::string out;
    for (const std::string& arg : argv) {
        if (!out.empty()) {
            out += ' ';
        }
        const bool simple{!arg.empty() &&
                          arg.find_first_of(" \t\"'") == std::string::npos};
        if (simple) {
            out += arg;
        } else {
            out += '"';
            for (const char c : arg) {
                if (c == '"') {
                    out += "\"\"";
                } else {
                    out += c;
                }
            }
            out += '"';
        }
    }
    return out;
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

} // namespace

void start_command(app_state& state, const run_kind kind,
                   const std::string& shown, std::vector<std::string> argv)
{
    Expects(!argv.empty());
    if (state.jobs.running()) {
        return;
    }
    append_log(state, "$ " + shown + "\n");
    state.kind = kind;
    if (kind == run_kind::patcher) {
        state.has_run = true;
    }
    if (!state.jobs.launch([argv = std::move(argv)] {
            return run_process(argv);
        })) {
        append_log(state, "error: a command is already running\n\n");
    }
}

void start_run(app_state& state, const char* subcommand)
{
    Expects(subcommand != nullptr);
    std::vector<std::string> argv{state.python, "-u", state.script_path,
                                  subcommand, "--install-dir",
                                  state.install_dir};
    if (std::string_view(subcommand) == "patch" && !state.version_dll.empty()) {
        argv.emplace_back("--version-dll");
        argv.emplace_back(state.version_dll);
    }
    start_command(state, run_kind::patcher, format_argv(argv), std::move(argv));
}

void start_wemod_download(app_state& state)
{
    if constexpr (is_windows) {
        // SDL_GetUserFolder returns SDL-owned memory - do not free.
        const char* const downloads{SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)};
        if (downloads == nullptr) {
            log_error(std::string{"SDL_GetUserFolder: "} + SDL_GetError());
            append_log(state,
                       "error: could not find the Downloads folder\n"
                       "  fix: grab the installer from "
                       "https://www.wemod.com/download\n\n");
            return;
        }
        const fs::path installer{fs::path(downloads) / "wemod_setup.exe"};
        // No shell: powershell argv, installer launched by the user flow
        // after download (log narrates the next step).
        const std::vector<std::string> argv{
            "powershell",      "-NoProfile",
            "-ExecutionPolicy", "Bypass",
            "-Command",
            "$ProgressPreference='SilentlyContinue'; "
            "Invoke-WebRequest -Uri '" +
                std::string(wemod_installer_url) + "' -OutFile '" +
                installer.string() + "'; Start-Process '" +
                installer.string() + "'"};
        start_command(state, run_kind::wemod, format_argv(argv), argv);
    } else {
        const char* home{SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "HOME")};
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
        open_url(std::string(launcher_repo_url).c_str());
        const std::vector<std::string> argv{
            "git", "clone", std::string(launcher_clone_url), dir.string()};
        start_command(state, run_kind::wemod, format_argv(argv), argv);
    }
}

void start_probe(app_state& state)
{
    const std::vector<std::string> argv{
        state.python, "-c",
        "import sys,platform;print(sys.version.split()[0]);"
        "print(platform.platform())"};
    start_command(state, run_kind::probe, format_argv(argv), argv);
}

void poll_run(app_state& state)
{
    run_result result;
    if (!state.jobs.try_take(result)) {
        return;
    }
    append_log(state, result.output);
    if (!result.output.empty() && !result.output.ends_with('\n')) {
        append_log(state, "\n");
    }
    append_log(state,
               "[exit code: " + std::to_string(result.exit_code) + "]\n\n");
    state.last_exit_code = result.exit_code;
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
            if (const char* home{SDL_GetEnvironmentVariable(
                    SDL_GetEnvironment(), "HOME")}) {
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

std::string env_info(const app_state& state)
{
    std::string info{"--- environment ---\n"};
    info += "gui version: ";
    info += std::string(gui_version) + "\n";
    info += "platform: " + platform_name() + " " + std::string(target_arch) +
        "\n";
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

void copy_output(app_state& state)
{
    if (!set_clipboard_text(state.log + "\n" + env_info(state))) {
        return;
    }
    state.copied_flash = 1.5F;
}

void clear_output(app_state& state)
{
    state.log.clear();
    state.copied_flash = 0.0F;
}

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
    open_url(url.c_str());
}

} // namespace wemod::gui
