// core.cpp - pure domain logic. No SDL headers, no ImGui headers.
// SDL is reached only through backend:: (env, paths, urls, dialogs).
//
// Threading: AppState::start_command() funnels into JobRunner, one
// std::jthread job at a time. The worker only returns a RunResult;
// the UI thread harvests it in core::poll(). No atomics, no detached
// threads: jthread joins on destruction / relaunch by construction.

#include "app.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <format>
#include <ranges>
#include <system_error>

#ifndef _WIN32
#include <sys/wait.h> // WIFEXITED / WEXITSTATUS for pclose()
#endif

namespace wemod::gui
{

// --- process capture --------------------------------------------------

RunResult run_capture(const std::string& command)
{
    RunResult result;
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
    while (fgets(buffer.data(),
                 static_cast<int>(buffer.size()), pipe) != nullptr) {
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

// --- JobRunner (std::jthread, RAII join) -------------------------------

JobRunner::~JobRunner()
{
    // jthread destructor joins; request stop first so a future
    // stop-aware worker can bail early. Current worker ignores the
    // token and runs to completion - join still bounds its lifetime.
    worker_.request_stop();
}

void JobRunner::launch(std::string command)
{
    if (active_) {
        return;
    }
    try {
        std::promise<RunResult> promise;
        pending_ = promise.get_future();
        // Move both into the worker: UI thread never touches them
        // again until try_take(). The previous jthread (if any) was
        // already joined, so assignment here never blocks on a
        // running job - active_ guards that.
        worker_ = std::jthread(
            [cmd = std::move(command),
             pr = std::move(promise)](std::stop_token) mutable {
                try {
                    pr.set_value(run_capture(cmd));
                } catch (...) {
                    // Never let an exception escape the thread.
                    try {
                        pr.set_exception(std::current_exception());
                    } catch (...) {
                    }
                }
            });
        active_ = true;
    } catch (...) {
        // Promise / thread creation failed: report as an immediate
        // failed result on the next poll instead of throwing out of
        // UI event handling.
        try {
            std::promise<RunResult> promise;
            pending_ = promise.get_future();
            promise.set_value(
                RunResult{-1, "error: could not start background job\n"});
            active_ = true;
        } catch (...) {
            active_ = false;
        }
    }
}

std::optional<RunResult> JobRunner::try_take() noexcept
{
    if (!active_) {
        return std::nullopt;
    }
    try {
        if (pending_.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
            return std::nullopt;
        }
        // Worker fulfilled the promise; join before touching state so
        // relaunch never overlaps a still-exiting thread.
        if (worker_.joinable()) {
            worker_.join();
        }
        active_ = false;
        return pending_.get();
    } catch (...) {
        // Worker exception or broken promise: failed result, job done.
        try {
            if (worker_.joinable()) {
                worker_.join();
            }
        } catch (...) {
        }
        active_ = false;
        return RunResult{-1, "error: background job failed\n"};
    }
}

// --- AppState ----------------------------------------------------------

void AppState::append_log(std::string_view text)
{
    log += text;
    if (log.size() > log_budget) {
        const std::size_t over{log.size() - log_budget};
        const std::size_t nl{log.find('\n', over)};
        log.erase(0, nl == std::string::npos ? over : nl + 1);
    }
}

void AppState::start_command(RunKind next_kind,
                             const std::string& shown,
                             std::string command)
{
    if (command.empty() || job.running()) {
        return;
    }
    append_log("$ " + shown + "\n");
    kind = next_kind;
    if (next_kind == RunKind::patcher) {
        has_run = true;
    }
    job.launch(std::move(command));
}

void AppState::start_run(const char* subcommand)
{
    if (subcommand == nullptr) {
        return;
    }
    std::string shown{python + " -u " + script_path + " " + subcommand +
                      " --install-dir " + install_dir};
    std::string command{core::shell_quote(python) + " -u " +
                        core::shell_quote(script_path) + " " + subcommand +
                        " --install-dir " + core::shell_quote(install_dir)};
    if (std::string_view(subcommand) == "patch" && !version_dll.empty()) {
        shown += " --version-dll " + version_dll;
        command += " --version-dll " + core::shell_quote(version_dll);
    }
    start_command(RunKind::patcher, shown, std::move(command));
}

void AppState::start_probe()
{
    const std::string shown{python +
                            " -c \"import sys,platform;print(sys.version."
                            "split()[0]);print(platform.platform())\""};
    start_command(RunKind::probe, shown,
                  core::shell_quote(python) +
                      " -c \"import sys,platform;print(sys.version.split()[0"
                      "]);print(platform.platform())\"");
}

void AppState::start_wemod_download()
{
    if constexpr (is_windows) {
        const fs::path downloads{backend::downloads_dir()};
        if (downloads.empty()) {
            backend::show_error("Download WeMod",
                                "Could not locate the Downloads folder.",
                                window);
            return;
        }
        const fs::path installer{downloads / "wemod_setup.exe"};
        const std::string command{
            "powershell -NoProfile -ExecutionPolicy Bypass -Command \""
            "$ProgressPreference='SilentlyContinue'; "
            "Invoke-WebRequest -Uri '" +
            std::string(wemod_installer_url) + "' -OutFile '" +
            installer.string() + "'; Start-Process '" +
            installer.string() + "'\""};
        start_command(RunKind::wemod, command, command);
    } else {
        const char* home{backend::env_var("HOME")};
        if (home == nullptr) {
            append_log("error: HOME is not set - cannot clone "
                       "wemod-launcher.\n\n");
            return;
        }
        const fs::path dir{fs::path(home) / "wemod-launcher"};
        std::error_code ec;
        if (fs::is_directory(dir, ec)) {
            // Already cloned: aim the field at it - the resolver
            // picks up wemod_data/wemod_bin once login happened.
            install_dir = dir.string();
            append_log("wemod-launcher already cloned: " + dir.string() +
                       "\n  run it once and log in - wemod_data/wemod_bin "
                       "appears after login, this field resolves to "
                       "it.\n\n");
            scroll_to_bottom = true;
            return;
        }
        // Tutorial opens alongside the clone, per the readme flow.
        backend::open_url(std::string(launcher_repo_url).c_str(), window);
        const std::string command{"git clone " +
                                  std::string(launcher_clone_url) + " " +
                                  core::shell_quote(dir.string())};
        start_command(RunKind::wemod, command, command);
    }
}

// --- core namespace ----------------------------------------------------

namespace core
{

std::string url_encode(std::string_view text)
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

std::string shell_quote(std::string_view arg)
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

std::vector<std::int32_t> version_parts(std::string_view name)
{
    constexpr std::string_view prefix{"app-"};
    if (name.starts_with(prefix)) {
        name.remove_prefix(prefix.size());
    }
    std::vector<std::int32_t> parts;
    std::size_t pos{0};
    while (pos < name.size()) {
        const std::size_t dot{name.find('.', pos)};
        const std::string_view token{
            name.data() + pos,
            (dot == std::string_view::npos ? name.size() : dot) - pos};
        std::int32_t value{0};
        const char* const begin{token.data()};
        const char* const end{begin + token.size()};
        if (const auto res{std::from_chars(begin, end, value)};
            res.ec != std::errc{} || res.ptr != end) {
            value = 0;
        }
        parts.push_back(value);
        if (dot == std::string_view::npos) {
            break;
        }
        pos = dot + 1;
    }
    return parts;
}

fs::path newest_app_dir(const fs::path& root)
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

std::string default_install_dir()
{
    if constexpr (is_windows) {
        if (const char* local{backend::env_var("LOCALAPPDATA")}) {
            return (fs::path(local) / "WeMod").string();
        }
    } else {
        if (const char* home{backend::env_var("HOME")}) {
            return (fs::path(home) / "wemod-launcher").string();
        }
    }
    return {};
}

fs::path resolve_wemod_dir(const std::string& dir)
{
    if (dir.empty()) {
        return {};
    }
    std::error_code ec;
    fs::path picked{dir};
    if (fs::is_regular_file(picked / "resources" / "app.asar", ec)) {
        return picked;
    }
    if (const fs::path app{newest_app_dir(picked)}; !app.empty()) {
        return app;
    }
    fs::path launcher_bin{picked / "wemod_data" / "wemod_bin"};
    if (fs::is_regular_file(launcher_bin / "resources" / "app.asar", ec)) {
        return launcher_bin;
    }
    return {};
}

fs::path bundled_script()
{
    return backend::exe_dir() / patcher_script_name;
}

fs::path bundled_version_dll()
{
    return backend::exe_dir() / version_dll_name;
}

void probe_filesystem(AppState& state)
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
    std::error_code ec;
    state.script_present = !state.script_path.empty() &&
        fs::is_regular_file(state.script_path, ec);
    state.dll_present = !state.version_dll.empty() &&
        fs::is_regular_file(state.version_dll, ec);
}

void parse_probe(AppState& state, const std::string& output)
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

void poll(AppState& state)
{
    std::optional<RunResult> done{state.job.try_take()};
    if (!done.has_value()) {
        return;
    }
    const RunResult& result{*done};
    state.append_log(result.output);
    if (!result.output.empty() && !result.output.ends_with('\n')) {
        state.append_log("\n");
    }
    state.append_log("[exit code: " + std::to_string(result.exit_code) +
                      "]\n\n");
    state.last_exit_code = result.exit_code;
    state.scroll_to_bottom = true;

    switch (state.kind) {
    case RunKind::wemod:
        if (result.exit_code != 0) {
            state.append_log(
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
            state.append_log("WeMod installer downloaded and started.\n"
                             "  next: install, run WeMod once, log in - then "
                             "this app auto-detects the folder.\n\n");
        } else {
            if (const char* home{backend::env_var("HOME")}) {
                state.install_dir =
                    (fs::path(home) / "wemod-launcher").string();
            }
            state.append_log("wemod-launcher cloned (tutorial opened in your "
                             "browser).\n"
                             "  next: run it once and log in - "
                             "wemod_data/wemod_bin appears after login, the "
                             "folder field resolves to it.\n\n");
        }
        break;
    case RunKind::probe:
        state.python_ok = result.exit_code == 0 ? ProbeState::works
                                                : ProbeState::failed;
        parse_probe(state, result.output);
        break;
    case RunKind::patcher:
        if (result.exit_code != 0) {
            state.append_log("hint: close WeMod fully, then retry. If it still "
                             "fails, press Report bug below - the issue opens "
                             "pre-filled with this log.\n\n");
        }
        break;
    }
}

std::string env_info(const AppState& state)
{
    std::string info{"--- environment ---\n"};
    info += "gui version: ";
    info += std::string(gui_version) + "\n";
    info += "platform: " + backend::platform_name() + " " +
        std::string(target_arch) + "\n";
    info += "exe dir: " + backend::exe_dir().string() + "\n";
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

void copy_output(AppState& state)
{
    if (!backend::set_clipboard(state.log + "\n" + env_info(state),
                                state.window)) {
        return;
    }
    state.copied_flash = 1.5F;
}

void clear_output(AppState& state) noexcept
{
    try {
        state.log.clear();
    } catch (...) {
    }
    state.copied_flash = 0.0F;
}

void report_bug(AppState& state)
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
    backend::open_url(url.c_str(), state.window);
}

std::string_view running_status(RunKind kind) noexcept
{
    switch (kind) {
    case RunKind::probe:
        return "Checking Python...";
    case RunKind::wemod:
        return is_windows ? std::string_view("Downloading WeMod...")
                          : std::string_view("Cloning wemod-launcher...");
    case RunKind::patcher:
        return "Running the patcher...";
    }
    return {};
}

const char* run_block_reason(bool install_ok, bool script_ok) noexcept
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
    return nullptr;
}

} // namespace core

} // namespace wemod::gui
