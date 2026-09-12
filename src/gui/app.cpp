#include "app.hpp"

#include <gsl/assert>
#include <gsl/narrow>

#include <algorithm>
#include <charconv>
#include <format>
#include <ranges>

namespace wemod::gui
{
std::string url_encode(const std::string_view text)
{
    constexpr std::string_view unreserved{"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                           "abcdefghijklmnopqrstuvwxyz"
                                           "0123456789-_.~"};
    std::string result;
    result.reserve(text.size());
    for (const unsigned char value : text) {
        if (unreserved.find(static_cast<char>(value)) !=
            std::string_view::npos) {
            result += static_cast<char>(value);
        } else {
            result += std::format("%{:02X}", value);
        }
    }
    return result;
}

std::string shell_quote(const std::string_view arg)
{
    std::string result{is_windows ? "\"" : "'"};
    for (const char value : arg) {
        if constexpr (is_windows) {
            result += value == '"' ? "\"\"" : std::string{value};
        } else {
            result += value == '\'' ? "'\"'\"'" : std::string{value};
        }
    }
    result += is_windows ? '"' : '\'';
    return result;
}

std::vector<std::int32_t> version_parts(std::string_view name)
{
    constexpr std::string_view prefix{"app-"};
    if (name.starts_with(prefix)) {
        name.remove_prefix(prefix.size());
    }

    std::vector<std::int32_t> result;
    while (!name.empty()) {
        const std::size_t dot{name.find('.')};
        const std::string_view token{name.substr(0, dot)};
        std::int32_t value{0};
        const auto parsed{std::from_chars(token.data(), token.data() +
                                                           token.size(),
                                          value)};
        if (parsed.ec != std::errc{} ||
            parsed.ptr != token.data() + token.size()) {
            value = 0;
        }
        result.push_back(value);
        if (dot == std::string_view::npos) {
            break;
        }
        name.remove_prefix(dot + 1UZ);
    }
    return result;
}

fs::path newest_app_dir(const fs::path& root)
{
    std::error_code error;
    std::vector<fs::path> candidates;
    for (fs::directory_iterator it{root, error}, end; it != end && !error;
         it.increment(error)) {
        std::error_code type_error;
        if (it->is_directory(type_error) && !type_error &&
            it->path().filename().string().starts_with("app-")) {
            candidates.push_back(it->path());
        }
    }

    const auto newest{std::ranges::max_element(
        candidates, {}, [](const fs::path& path) {
            return version_parts(path.filename().string());
        })};
    return newest == candidates.end() ? fs::path{} : fs::path{*newest};
}

fs::path resolve_wemod_dir(const std::string_view dir)
{
    if (dir.empty()) {
        return {};
    }

    std::error_code error;
    fs::path picked{dir};
    if (fs::is_regular_file(picked / "resources" / "app.asar", error)) {
        return picked;
    }
    if (const fs::path app{newest_app_dir(picked)}; !app.empty()) {
        return app;
    }
    const fs::path launcher{picked / "wemod_data" / "wemod_bin"};
    error.clear();
    return fs::is_regular_file(launcher / "resources" / "app.asar", error)
        ? launcher
        : fs::path{};
}

void append_log(app_state& state, const std::string_view text)
{
    state.log += text;
    if (state.log.size() <= log_budget) {
        return;
    }
    const std::size_t over{state.log.size() - log_budget};
    const std::size_t newline{state.log.find('\n', over)};
    state.log.erase(0, newline == std::string::npos ? over : newline + 1UZ);
}

std::string env_info(const app_state& state)
{
    std::string info{"--- environment ---\n"};
    info += std::format("gui version: {}\n", gui_version);
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

std::string issue_url(const app_state& state)
{
    const std::size_t start{state.log.size() > issue_log_budget
                                ? state.log.size() - issue_log_budget
                                : 0UZ};
    const std::string_view tail{state.log.data() + start,
                                state.log.size() - start};
    const std::string prefix{start == 0UZ ? "" : "... (log tail)\n"};
    const std::string body{std::format(
        "## Summary\n\nDescribe what failed and what you expected.\n\n"
        "## Steps to reproduce\n\n1. \n2. \n3. \n\n"
        "## Diagnostic digest\n\n```text\n{}\n```\n\n"
        "## Log output\n\n```text\n{}{}\n```",
        env_info(state), prefix, tail)};
    return std::format("{}?template=bug_report.yml&title={}&body={}",
                       issue_new_url, url_encode("bug: gui report"),
                       url_encode(body));
}

void parse_probe(app_state& state, const std::string_view output)
{
    const auto line = [output](const std::size_t from) {
        const std::size_t end{output.find('\n', from)};
        std::string_view value{output.substr(from, end - from)};
        while (!value.empty() &&
               (value.back() == '\r' || value.back() == ' ')) {
            value.remove_suffix(1UZ);
        }
        return value;
    };

    if (const std::string_view version{line(0UZ)}; !version.empty()) {
        state.python_version = std::format("Python {}", version);
    }
    if (const std::size_t end{output.find('\n')};
        end != std::string_view::npos) {
        state.platform_detail = line(end + 1UZ);
    }
}

background_runner::~background_runner() noexcept
{
    request_stop();
}

bool background_runner::busy() const noexcept
{
    const std::lock_guard lock{mutex_};
    return busy_;
}

bool background_runner::launch(const run_kind kind, std::string command)
{
    Expects(!command.empty());
    {
        const std::lock_guard lock{mutex_};
        if (busy_) {
            return false;
        }
        busy_ = true;
    }

    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::jthread{
        [this, kind, command = std::move(command)](
            const std::stop_token& token) {
            run_result result{run_capture(command, token)};
            const std::lock_guard lock{mutex_};
            if (token.stop_requested()) {
                busy_ = false;
                return;
            }
            finished_.push(finished_job{kind, std::move(result)});
        }};
    return true;
}

std::optional<background_runner::finished_job> background_runner::poll()
{
    const std::lock_guard lock{mutex_};
    if (finished_.empty()) {
        return std::nullopt;
    }
    finished_job result{std::move(finished_.front())};
    finished_.pop();
    if (finished_.empty()) {
        busy_ = false;
    }
    return result;
}

void background_runner::request_stop() noexcept
{
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    const std::lock_guard lock{mutex_};
    busy_ = false;
}
}
