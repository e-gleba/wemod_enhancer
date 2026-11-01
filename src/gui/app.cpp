#include "app.hpp"

#include <gsl/assert>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <ranges>

namespace wemod::gui
{
std::string url_encode(const std::string_view text)
{
    constexpr std::string_view unreserved = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                           "abcdefghijklmnopqrstuvwxyz"
                                           "0123456789-_.~"};
    std::string result;
    result.reserve(text.size());
    for (const char raw : text) {
        const auto byte = static_cast<std::uint8_t>(raw);
        const char value = static_cast<char>(byte);
        if (unreserved.contains(value)) {
            result += value;
        } else {
            result += std::format("%{:02X}", byte);
        }
    }
    return result;
}

// One numeric component; any junk (empty, trailing chars, overflow)
// maps to 0 exactly like the previous inline parse did.
[[nodiscard]] constexpr std::int32_t
parse_version_part(const std::string_view token) noexcept
{
    std::int32_t value = 0;
    const auto parsed = std::from_chars(
        token.data(), token.data() + token.size(), value);
    const bool complete = parsed.ec == std::errc{} &&
        parsed.ptr == token.data() + token.size();
    return complete ? value : 0;
}

static_assert(parse_version_part("12") == 12);
static_assert(parse_version_part("x") == 0);
static_assert(parse_version_part("1x") == 0);
static_assert(parse_version_part("") == 0);

std::vector<std::int32_t> version_parts(std::string_view name)
{
    constexpr std::string_view prefix = "app-";
    if (name.starts_with(prefix)) {
        name.remove_prefix(prefix.size());
    }

    std::vector<std::int32_t> result = {};
    while (!name.empty()) {
        const std::size_t dot = name.find('.');
        result.push_back(parse_version_part(name.substr(0, dot)));
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
        if (!it->is_directory(type_error) || type_error ||
            !it->path().filename().string().starts_with("app-")) {
            continue;
        }
        // Only real installs: the asar must exist, not just the name.
        std::error_code asar_error;
        if (fs::is_regular_file(it->path() / "resources" / "app.asar",
                                asar_error)) {
            candidates.push_back(it->path());
        }
    }

    const auto newest = std::ranges::max_element(
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
    fs::path picked = dir;
    // Direct hit: an app-x.y.z folder or a (custom) wemod_bin.
    if (fs::is_regular_file(picked / "resources" / "app.asar", error)) {
        return picked;
    }
    // One level up from wemod_bin (the wemod_data folder itself).
    error.clear();
    fs::path nested = picked / "wemod_bin";
    if (fs::is_regular_file(nested / "resources" / "app.asar", error)) {
        return nested;
    }
    // WeMod root: newest version that actually contains the asar.
    if (const fs::path app{newest_app_dir(picked)}; !app.empty()) {
        return app;
    }
    // Launcher checkout root.
    error.clear();
    const fs::path launcher = picked / "wemod_data" / "wemod_bin";
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
    const std::size_t over = state.log.size() - log_budget;
    const std::size_t newline = state.log.find('\n', over);
    state.log.erase(0, newline == std::string::npos ? over : newline + 1UZ);
}

std::string env_info(const app_state& state)
{
    const std::string_view install_dir = state.install_dir.empty()
        ? std::string_view{"<not set>"}
        : std::string_view{state.install_dir};
    const std::string_view script_path = state.script_path.empty()
        ? std::string_view{"<missing next to exe>"}
        : std::string_view{state.script_path};
    const std::string_view python_version = state.python_version.empty()
        ? std::string_view{"<not probed>"}
        : std::string_view{state.python_version};

    std::string info = "--- environment ---\n";
    std::format_to(std::back_inserter(info), "gui version: {}\n",
                   gui_version);
    std::format_to(std::back_inserter(info), "platform: {} {}\n",
                   state.platform_name, state.platform_detail);
    std::format_to(std::back_inserter(info), "exe dir: {}\n",
                   state.exe_dir_text);
    std::format_to(std::back_inserter(info), "wemod folder: {}\n",
                   install_dir);
    std::format_to(std::back_inserter(info), "patcher script: {}\n",
                   script_path);
    std::format_to(std::back_inserter(info), "python command: {}\n",
                   state.python);
    std::format_to(std::back_inserter(info), "python: {}\n",
                   python_version);
    if (state.has_run) {
        std::format_to(std::back_inserter(info), "last exit code: {}\n",
                       state.last_exit_code);
    }
    return info;
}

std::string report_text(const app_state& state)
{
    return std::format("{}\n{}", state.log, env_info(state));
}

std::string issue_url(const app_state& state)
{
    // Issue forms ignore `body`; fields prefill by their `id` instead.
    // See: https://docs.github.com/en/issues/tracking-your-work-with-issues/using-issues/creating-an-issue#creating-an-issue-from-a-url-query
    const std::size_t start = state.log.size() > issue_log_budget
                                ? state.log.size() - issue_log_budget
                                : 0UZ};
    const std::string_view tail = std::string_view{state.log}.substr(start);
    const std::string prefix = start == 0UZ ? "" : "... (log tail)\n";
    const std::string diagnostics =
        std::format("{}{}{}", env_info(state), prefix, tail)};
    const std::string os = state.platform_detail.empty()
                             ? state.platform_name
                             : std::format("{} {}", state.platform_name,
                                           state.platform_detail)};
    return std::format(
        "{}?template=bug_report.yml&title={}&version={}&os={}&diagnostics={}",
        issue_new_url, url_encode("bug: gui report"),
        url_encode(gui_version), url_encode(os),
        url_encode(diagnostics));
}

void parse_probe(app_state& state, const std::string_view output)
{
    const auto line = [output](const std::size_t from) {
        const std::size_t end = output.find('\n', from);
        std::string_view value = output.substr(from, end - from);
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

bool try_next_python(app_state& state) noexcept
{
    const std::size_t next = state.python_attempt + 1UZ;
    if (next >= python_candidates.size()) {
        return false;
    }
    state.python_attempt = next;
    state.python = python_candidates[next];
    return true;
}

background_runner::~background_runner() noexcept
{
    request_stop();
}

bool background_runner::busy() const noexcept
{
    const std::scoped_lock lock{mutex_};
    return busy_;
}

bool background_runner::launch(const run_kind kind,
                               std::vector<std::string> args)
{
    Expects(!args.empty());
    {
        const std::scoped_lock lock{mutex_};
        if (busy_) {
            return false;
        }
        busy_ = true;
    }

    if (worker_.joinable()) {
        worker_.join();
    }
    worker_ = std::jthread{
        [this, kind, args = std::move(args)](
            const std::stop_token& token) {
            run_result result{run_capture(args, token)};
            const std::scoped_lock lock{mutex_};
            if (token.stop_requested()) {
                busy_ = false;
                return;
            }
            finished_.push(finished_job{.kind = kind, .result = std::move(result)});
        }};
    return true;
}

std::optional<background_runner::finished_job> background_runner::poll()
{
    const std::scoped_lock lock{mutex_};
    if (finished_.empty()) {
        return std::nullopt;
    }
    finished_job result = std::move(finished_.front());
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
    const std::scoped_lock lock{mutex_};
    busy_ = false;
}
}
