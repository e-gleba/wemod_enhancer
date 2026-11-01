// WeMod Enhancer - Dear ImGui view side (ImGui + app.hpp only).
// Steam-style single column: header, folder, actions, output,
// advanced. Side effects leave via the state outbox for the SDL side.

#include "app.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <string_view>

namespace wemod::gui::view
{

namespace
{

#if defined(__x86_64__) || defined(_M_X64)
constexpr std::string_view arch{"x86_64"};
#elif defined(__aarch64__) || defined(_M_ARM64)
constexpr std::string_view arch{"arm64"};
#else
constexpr std::string_view arch{"unknown"};
#endif

// Status semantics only, sampled from the stock dark palette so text
// and fields stay readable without a custom theme to maintain.
constexpr std::array<float, 4> tint_ok{0.16F, 0.26F, 0.16F, 0.90F};
constexpr std::array<float, 4> tint_bad{0.30F, 0.14F, 0.14F, 0.90F};
constexpr std::array<float, 4> ink_ok{0.40F, 0.80F, 0.40F, 1.00F};
constexpr std::array<float, 4> ink_bad{0.90F, 0.35F, 0.35F, 1.00F};
constexpr std::array<float, 4> ink_dim{0.55F, 0.55F, 0.55F, 1.00F};

[[nodiscard]] ImVec4 to_vec(const std::array<float, 4>& color)
{
    const auto& [red, green, blue, alpha]{color};
    return {red, green, blue, alpha};
}

void muted(const std::string_view text)
{
    // Wrap at the available edge, not the window edge, so this stays
    // correct inside table columns as well as full-width rows.
    const float wrap{ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x};
    ImGui::PushStyleColor(ImGuiCol_Text, to_vec(ink_dim));
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void hover_tip(const std::string_view text, const bool allow_disabled = false)
{
    if (!ImGui::IsItemHovered(allow_disabled
                                  ? ImGuiHoveredFlags_AllowWhenDisabled
                                  : ImGuiHoveredFlags_None)) {
        return;
    }
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0F);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// (?) marker: hover shows help, click opens the docs URL.
void help_link(app_state& state, const std::string_view text,
               const std::string_view url)
{
    ImGui::SameLine();
    muted("(?)");
    hover_tip(text);
    if (ImGui::IsItemClicked()) {
        state.want_open_url = std::string{url};
    }
}

enum class status_kind : std::uint8_t { none, ok, failed };

// ASCII status tag, airborne-console style. Plain text keeps every
// column aligned in any font, at any DPI: OK, ERR, -- states.
[[nodiscard]] constexpr std::string_view
status_label(const status_kind kind) noexcept
{
    switch (kind) {
    case status_kind::ok:
        return "OK";
    case status_kind::failed:
        return "ERR";
    case status_kind::none:
        return "--";
    }
    return "--";
}

void status_tag(const status_kind kind)
{
    const std::string_view label{status_label(kind)};
    ImVec4 tint{to_vec(ink_dim)};
    if (kind == status_kind::ok) {
        tint = to_vec(ink_ok);
    } else if (kind == status_kind::failed) {
        tint = to_vec(ink_bad);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, tint);
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0F, ImGui::GetFontSize() * 0.6F);
}

[[nodiscard]] float equal_width(const std::size_t count)
{
    if (count == 0UZ) {
        return 1.0F;
    }
    const float gaps{ImGui::GetStyle().ItemSpacing.x *
                     static_cast<float>(count - 1UZ)};
    return std::max((ImGui::GetContentRegionAvail().x - gaps) /
                        static_cast<float>(count),
                    1.0F);
}

// Full-width field, ok/bad tint once a value was entered.
void field(const char* id, const char* hint, std::string& value,
           const bool show_tint, const bool ok, const char* why)
{
    if (show_tint) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
                              to_vec(ok ? tint_ok : tint_bad));
    }
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint(id, hint, &value);
    if (show_tint) {
        ImGui::PopStyleColor();
        if (!ok) {
            hover_tip(why);
        }
    }
}

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
    std::error_code error;
    state.script_present = !state.script_path.empty() &&
        fs::is_regular_file(state.script_path, error);
}

[[nodiscard]] bool resolve_for_run(app_state& state)
{
    if (state.resolved_install_dir.empty()) {
        return false;
    }
    state.install_dir = state.resolved_install_dir.string();
    return true;
}

void draw_settings(app_state& state)
{
    muted("Patcher");
    help_link(state,
              "wemod_enhancer.py ships next to the executable. Re-download "
              "the GUI package if this stays red.",
              releases_url);
    field("##script", "wemod_enhancer.py next to the exe",
          state.script_path, true, state.script_present,
          "Patcher script not found");

    muted("Python command");
    help_link(state,
              "Runs the patcher. Default: python on Windows, python3 "
              "elsewhere. Use a full path when Python is not on PATH.",
              python_url);
    field("##python", "python / python3", state.python,
          state.python_ok != probe_state::unknown,
          state.python_ok == probe_state::works, "Python failed to start");
}

} // namespace

frame_requests draw(app_state& state)
{
    frame_requests requests;
    const ImGuiViewport* viewport{ImGui::GetMainViewport()};
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags flags{
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings};
    ImGui::Begin("##main", nullptr, flags);

    const float action_height{ImGui::GetFrameHeight() * 1.55F};

    probe_filesystem(state);
    const bool install_ok{!state.resolved_install_dir.empty()};
    const bool script_ok{state.script_present};

    ImGui::TextUnformatted("WeMod Enhancer");
    {
        const std::string right{std::format("v{} | {} {}", gui_version,
                                            state.platform_name, arch)};
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             ImGui::GetContentRegionAvail().x -
                             ImGui::CalcTextSize(right.c_str()).x);
        muted(right);
    }
    ImGui::Separator();

    muted("WeMod folder");
    help_link(state,
              "Anything with resources/app.asar inside: an app-x.y.z "
              "folder, a custom wemod_bin, or the launcher root. The "
              "newest installed version is picked automatically.",
              quickstart_url);
    const float browse_width{std::max(
        ImGui::CalcTextSize("Browse...").x +
            ImGui::GetStyle().FramePadding.x * 2.0F + 24.0F,
        96.0F)};
    const bool tinted{!state.install_dir.empty()};
    if (tinted) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
                              to_vec(install_ok ? tint_ok : tint_bad));
    }
    ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x -
                                         browse_width -
                                         ImGui::GetStyle().ItemSpacing.x,
                                     ImGui::GetFontSize() * 8.0F));
    ImGui::InputTextWithHint("##install", "path to WeMod",
                             &state.install_dir);
    if (tinted) {
        ImGui::PopStyleColor();
        if (!install_ok) {
            hover_tip("Not a WeMod install");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(browse_width, 0.0F))) {
        state.want_browse = true;
    }
    if (install_ok) {
        if (state.resolved_install_dir.string() != state.install_dir) {
            muted(state.resolved_install_dir.string());
        }
    } else if (state.install_dir.empty()) {
        muted("No folder selected - pick one above, or fetch WeMod below.");
    } else {
        std::error_code missing;
        const bool exists{fs::exists(fs::path{state.install_dir}, missing)};
        if (!exists) {
            muted("Folder does not exist - fetch WeMod with the button "
                  "below.");
        } else {
            muted("No resources/app.asar here. Launch WeMod once, log in, "
                  "then retry.");
        }
    }

    ImGui::Spacing();
    const std::size_t actions{install_ok ? 2UZ : 3UZ};
    const float width{equal_width(actions)};
    const std::string_view blocked{run_block_reason(install_ok, script_ok)};
    const bool actions_ready{!state.running && blocked.empty()};
    ImGui::BeginDisabled(!actions_ready);
    const bool patch_pressed{ImGui::Button("Patch",
                                           ImVec2(width, action_height))};
    if (patch_pressed && resolve_for_run(state)) {
        requests.patch = true;
    }
    if (!blocked.empty()) {
        hover_tip(blocked, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore", ImVec2(width, action_height)) &&
        resolve_for_run(state)) {
        requests.restore = true;
    }
    if (!blocked.empty()) {
        hover_tip(blocked, true);
    }
    ImGui::EndDisabled();
    // Fetching WeMod needs neither a folder nor the patcher script, so it
    // stays available whenever nothing else is running.
    if (!install_ok) {
        ImGui::SameLine();
        ImGui::BeginDisabled(state.running);
        if (ImGui::Button(is_windows ? "Get WeMod" : "Get launcher",
                          ImVec2(width, action_height))) {
            requests.download = true;
        }
        ImGui::EndDisabled();
        hover_tip(is_windows
                      ? "Download the official installer into Downloads"
                      : "Clone wemod-launcher and open the setup guide",
                  true);
    }

    ImGui::Spacing();
    if (state.running) {
        status_tag(status_kind::none);
        muted(running_status(state.kind));
    } else if (state.has_run) {
        if (state.last_exit_code == 0) {
            status_tag(status_kind::ok);
            ImGui::TextUnformatted("Done - launch WeMod, Pro is active.");
        } else {
            status_tag(status_kind::failed);
            const std::string failed{std::format("Failed (exit {})",
                                                 state.last_exit_code)};
            ImGui::TextUnformatted(failed.c_str());
        }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Advanced")) {
        ImGui::Indent(8.0F);
        draw_settings(state);
        ImGui::Unindent(8.0F);
    }

    muted("Output");
    help_link(state, "Live patcher output. Copy it when reporting a bug.",
              issue_new_url);
    const float line{ImGui::GetTextLineHeightWithSpacing()};
    const float log_height{std::max(ImGui::GetContentRegionAvail().y -
                                        (action_height + line * 3.0F),
                                    line * 4.0F)};
    ImGui::BeginChild("##log", ImVec2(0.0F, log_height),
                      ImGuiChildFlags_Borders);
    if (state.log.empty()) {
        muted("awaiting command_");
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

    ImGui::Spacing();
    const float tool_width{equal_width(3UZ)};
    if (ImGui::Button("Copy", ImVec2(tool_width, 0.0F))) {
        requests.copy = true;
    }
    hover_tip("Copy log + environment info");
    ImGui::SameLine();
    ImGui::BeginDisabled(state.log.empty());
    if (ImGui::Button("Clear", ImVec2(tool_width, 0.0F))) {
        requests.clear = true;
    }
    ImGui::EndDisabled();
    hover_tip("Clear the log", true);
    ImGui::SameLine();
    if (ImGui::Button("Report bug", ImVec2(tool_width, 0.0F))) {
        requests.report = true;
    }
    hover_tip("Copy diagnostics + open a pre-filled issue");
    if (state.copied_flash > 0.0F) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, to_vec(ink_ok));
        ImGui::TextUnformatted("Copied!");
        ImGui::PopStyleColor();
    }

    ImGui::End();
    return requests;
}

} // namespace wemod::gui::view
