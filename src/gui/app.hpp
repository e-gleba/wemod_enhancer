#pragma once

#include "meta.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace wemod::gui
{
namespace fs = std::filesystem;

enum class run_kind : std::uint8_t { patcher, probe, wemod };
enum class probe_state : std::uint8_t { unknown, failed, works };

struct run_result final
{
    std::int32_t exit_code = -1;
    std::string output;
};

struct alert_request final
{
    std::string title;
    std::string message;
};

struct dialog_result final
{
    std::mutex mutex;
    std::optional<std::string> folder;
    bool pending = false;
};

struct app_state final
{
    std::string install_dir;
    std::string script_path;
    std::string python = std::string{python_candidates.front()};
    std::string log;
    bool running = false;
    run_kind kind = run_kind::patcher;
    bool scroll_to_bottom = false;
    bool has_run = false;
    std::int32_t last_exit_code = 0;
    float copied_flash = 0.0F;
    std::string probed_install_dir;
    std::string probed_script_path;
    fs::path resolved_install_dir;
    bool script_present = false;
    std::chrono::steady_clock::time_point last_probe;
    probe_state python_ok = probe_state::unknown;
    std::size_t python_attempt = 0UZ;
    std::string python_version;
    std::string platform_detail;
    std::string platform_name;
    std::string exe_dir_text;
    bool want_browse = false;
    std::optional<std::string> want_open_url;
    std::optional<std::string> want_clipboard;
    std::optional<alert_request> want_alert;
};

struct frame_requests final
{
    bool patch = false;
    bool restore = false;
    bool download = false;
    bool copy = false;
    bool clear = false;
    bool report = false;
};

[[nodiscard]] constexpr std::string_view
running_status(run_kind kind) noexcept
{
    switch (kind) {
    case run_kind::probe:
        return "Checking Python...";
    case run_kind::wemod:
        return is_windows ? std::string_view{"Downloading WeMod..."}
                          : std::string_view{"Cloning wemod-launcher..."};
    case run_kind::patcher:
        return "Running the patcher...";
    }
    return {};
}

[[nodiscard]] constexpr std::string_view
run_block_reason(bool install_ok, bool script_ok) noexcept
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

[[nodiscard]] std::string url_encode(std::string_view text);
[[nodiscard]] std::vector<std::int32_t> version_parts(std::string_view name);
[[nodiscard]] fs::path newest_app_dir(const fs::path& root);
[[nodiscard]] fs::path resolve_wemod_dir(std::string_view dir);
void append_log(app_state& state, std::string_view text);
[[nodiscard]] std::string env_info(const app_state& state);
[[nodiscard]] std::string report_text(const app_state& state);
[[nodiscard]] std::string issue_url(const app_state& state);
void parse_probe(app_state& state, std::string_view output);
// Advance to the next untried candidate after a failed probe.
// Returns false when the list is exhausted (python left as-is).
[[nodiscard]] bool try_next_python(app_state& state) noexcept;
[[nodiscard]] run_result run_capture(const std::vector<std::string>& args,
                                     const std::stop_token& token);

class background_runner final
{
  public:
    struct finished_job final
    {
        run_kind kind = run_kind::patcher;
        run_result result;
    };

    background_runner() = default;
    background_runner(const background_runner&) = delete;
    background_runner& operator=(const background_runner&) = delete;
    background_runner(background_runner&&) = delete;
    background_runner& operator=(background_runner&&) = delete;
    ~background_runner() noexcept;

    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool launch(run_kind kind,
                              std::vector<std::string> args);
    [[nodiscard]] std::optional<finished_job> poll();
    void request_stop() noexcept;

  private:
    mutable std::mutex mutex_;
    std::jthread worker_;
    std::queue<finished_job> finished_;
    bool busy_ = false;
};

namespace view
{
[[nodiscard]] frame_requests draw(app_state& state);
}

namespace platform
{
struct context final
{
    void* window = nullptr;
    void* renderer = nullptr;
    std::unique_ptr<dialog_result> dialog;
};

[[nodiscard]] std::pair<std::int32_t, std::int32_t>
preferred_size() noexcept;
[[nodiscard]] bool init(context& ctx, app_state& state) noexcept;
void drain_outbox(context& ctx, app_state& state,
                  float delta_seconds) noexcept;
void begin_frame(context& ctx, float scale_x, float scale_y,
                 const std::array<float, 4>& clear) noexcept;
void end_frame(context& ctx) noexcept;
void shutdown(context& ctx) noexcept;
void fatal(std::string_view title, std::string_view message) noexcept;
[[nodiscard]] std::string exe_dir();
[[nodiscard]] std::string default_install_dir();
[[nodiscard]] std::string platform_name();
void persist_log(const app_state& state) noexcept;
}
}
