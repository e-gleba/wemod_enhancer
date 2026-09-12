// WeMod Enhancer - Dear ImGui view side (ImGui + app.hpp only).
// No SDL headers here, no UI constants in the header. Reads
// app_state, draws the window, returns frame_requests; side effects
// leave via the state outbox for the SDL platform side to drain.

#include "app.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
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

constexpr float button_padding{24.0F};
constexpr float section_indent{16.0F};
constexpr float row_height_scale{1.55F};

constexpr std::array<float, 4> field_ok{0.14F, 0.32F, 0.16F, 0.70F};
constexpr std::array<float, 4> field_error{0.32F, 0.14F, 0.14F, 0.70F};
constexpr std::array<float, 4> color_ok{0.35F, 0.85F, 0.45F, 1.00F};
constexpr std::array<float, 4> color_error{0.90F, 0.30F, 0.30F, 1.00F};

[[nodiscard]] ImVec4 to_vec(const std::array<float, 4>& color)
{
    return {color[0], color[1], color[2], color[3]};
}

void text_colored(const std::array<float, 4>& color,
                  const std::string_view text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, to_vec(color));
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopStyleColor();
}

void text_disabled(const std::string_view text)
{
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void tooltip_text(const std::string_view text)
{
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0F);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool action_button(const char* label, const float width, const float height)
{
    return ImGui::Button(label, ImVec2(width, height));
}

[[nodiscard]] float equal_button_width(const int count)
{
    if (count <= 0) {
        return 1.0F;
    }
    const ImGuiStyle& style{ImGui::GetStyle()};
    const float available{ImGui::GetContentRegionAvail().x};
    const float gaps{style.ItemSpacing.x * static_cast<float>(count - 1)};
    return std::max((available - gaps) / static_cast<float>(count), 1.0F);
}

void field_label(const char* label)
{
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

void help_marker(app_state& state, const std::string_view text,
                 const std::string_view url)
{
    ImGui::SameLine();
    text_disabled("(?)");
    if (ImGui::IsItemHovered()) {
        tooltip_text(text);
    }
    if (ImGui::IsItemClicked()) {
        state.want_open_url = std::string{url};
    }
}

void push_field_tint(const bool ok)
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg,
                          to_vec(ok ? field_ok : field_error));
}

void field_fail_hover(const bool ok, const std::string_view why)
{
    if (!ok && ImGui::IsItemHovered()) {
        tooltip_text(why);
    }
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
    std::error_code error;
    state.script_present = !state.script_path.empty() &&
        fs::is_regular_file(state.script_path, error);
    error.clear();
    state.dll_present = !state.version_dll.empty() &&
        fs::is_regular_file(state.version_dll, error);
}

void draw_settings(app_state& state)
{
    field_label("Patcher");
    help_marker(state,
                "wemod_enhancer.py ships next to the executable. Re-download "
                "the GUI package if the field stays red.",
                releases_url);
    push_field_tint(state.script_present);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##script_path",
                             "wemod_enhancer.py next to the exe",
                             &state.script_path);
    ImGui::PopStyleColor();
    field_fail_hover(state.script_present, "Patcher script not found");

    ImGui::Spacing();

    field_label("Python command");
    help_marker(state,
                "The interpreter that runs the patcher. Default: python on "
                "Windows, python3 elsewhere. Point it at a full path if "
                "Python is not on PATH.",
                python_url);
    if (state.python_ok == probe_state::works) {
        ImGui::SameLine();
        text_disabled(state.python_version);
    }
    const bool tinted{state.python_ok != probe_state::unknown};
    const bool ok{state.python_ok == probe_state::works};
    if (tinted) {
        push_field_tint(ok);
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##python", "python / python3", &state.python);
    if (tinted) {
        ImGui::PopStyleColor();
        field_fail_hover(ok, "Python failed to start");
    }

    ImGui::Spacing();

    field_label("version.dll");
    help_marker(state,
                "The proxy DLL the patcher drops next to WeMod. Default: the "
                "copy next to the executable.",
                readme_url);
    push_field_tint(state.dll_present);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##version_dll", "version.dll next to the exe",
                             &state.version_dll);
    ImGui::PopStyleColor();
    field_fail_hover(state.dll_present, "version.dll not found");
}

[[nodiscard]] bool resolve_for_run(app_state& state)
{
    if (state.resolved_install_dir.empty()) {
        return false;
    }
    state.install_dir = state.resolved_install_dir.string();
    return true;
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

    const ImGuiStyle& style{ImGui::GetStyle()};
    const float row_height{ImGui::GetFrameHeight() * row_height_scale};

    probe_filesystem(state);
    const fs::path& resolved{state.resolved_install_dir};
    const bool install_ok{!resolved.empty()};
    const bool script_ok{state.script_present};

    field_label("WeMod folder");
    help_marker(state,
                "The app-x.y.z folder with resources/app.asar inside. Pick the "
                "WeMod root and the newest version is used automatically. "
                "Linux: the wemod-launcher clone works too - after the first "
                "run + login its wemod_data/wemod_bin is picked up.",
                quickstart_url);

    const float browse_width{ImGui::CalcTextSize("Browse...").x +
                             (style.FramePadding.x * 2.0F) + button_padding};
    const float path_width{
        std::max(ImGui::GetContentRegionAvail().x - browse_width -
                     style.ItemSpacing.x,
                 ImGui::GetFontSize() * 8.0F)};
    const bool tinted{!state.install_dir.empty()};
    if (tinted) {
        push_field_tint(install_ok);
    }
    ImGui::SetNextItemWidth(path_width);
    ImGui::InputTextWithHint("##install_dir", "path to WeMod",
                             &state.install_dir);
    if (tinted) {
        ImGui::PopStyleColor();
        field_fail_hover(install_ok, "Not a WeMod install");
    }
    ImGui::SameLine();
    if (action_button("Browse...", browse_width, 0.0F)) {
        state.want_browse = true;
    }
    if (install_ok && resolved.string() != state.install_dir) {
        text_disabled(resolved.string());
    }

    ImGui::Spacing();
    const int action_count{install_ok ? 2 : 3};
    const float action_width{equal_button_width(action_count)};
    const std::string_view block_reason{
        run_block_reason(install_ok, script_ok)};
    const bool patch_blocked{state.running || !block_reason.empty()};
    ImGui::BeginDisabled(patch_blocked);
    if (action_button("Patch", action_width, row_height) &&
        resolve_for_run(state)) {
        requests.patch = true;
    }
    ImGui::EndDisabled();
    if (!block_reason.empty() &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip_text(block_reason);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(patch_blocked);
    if (action_button("Restore", action_width, row_height) &&
        resolve_for_run(state)) {
        requests.restore = true;
    }
    ImGui::EndDisabled();
    if (!block_reason.empty() &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip_text(block_reason);
    }
    if (!install_ok) {
        ImGui::SameLine();
        ImGui::BeginDisabled(state.running);
        if (action_button("Download WeMod", action_width, row_height)) {
            requests.download = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            tooltip_text(
                is_windows
                    ? std::string_view{"Download the official WeMod installer "
                                       "into your Downloads folder and run it"}
                    : std::string_view{"Clone wemod-launcher into "
                                       "~/wemod-launcher and open the setup "
                                       "tutorial"});
        }
    }

    ImGui::Spacing();
    if (state.running) {
        text_disabled(running_status(state.kind));
    } else if (state.has_run) {
        if (state.last_exit_code == 0) {
            text_colored(color_ok, "Done. Launch WeMod - Pro is active.");
        } else {
            text_colored(color_error, std::format("Failed (exit code {})",
                                                  state.last_exit_code));
        }
    } else {
        text_disabled("Patch, then launch WeMod.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Settings")) {
        ImGui::Indent(section_indent);
        draw_settings(state);
        ImGui::Unindent(section_indent);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    field_label("Console");
    help_marker(state,
                "Live stdout and stderr from the patcher, the Python probe, "
                "and Download WeMod. Copy it below if something fails.",
                issue_new_url);

    const float line_height{ImGui::GetTextLineHeightWithSpacing()};
    const float toolbar_height{row_height + line_height +
                               (style.ItemSpacing.y * 3.0F)};
    const float log_height{
        std::max(ImGui::GetContentRegionAvail().y - toolbar_height,
                 line_height * 4.0F)};
    ImGui::BeginChild("##log", ImVec2(0.0F, log_height),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (state.log.empty()) {
        text_disabled("Patch output appears here. Copy it with the button "
                      "below when something fails.");
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
    const float utility_width{equal_button_width(3)};
    if (action_button("Copy output", utility_width, row_height)) {
        requests.copy = true;
    }
    if (ImGui::IsItemHovered()) {
        tooltip_text("Copy the log and environment info to the clipboard");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.log.empty());
    if (action_button("Clear output", utility_width, row_height)) {
        requests.clear = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip_text("Clear the log");
    }
    ImGui::SameLine();
    if (action_button("Report bug", utility_width, row_height)) {
        requests.report = true;
    }
    if (ImGui::IsItemHovered()) {
        tooltip_text("Open a pre-filled GitHub issue with the log attached");
    }

    ImGui::Spacing();
    const float footer_y{ImGui::GetCursorPosY()};
    if (state.copied_flash > 0.0F) {
        text_colored(color_ok, "Copied!");
    }
    {
        const std::string version{std::format("v{}", gui_version)};
        const float text_width{ImGui::CalcTextSize(version.c_str()).x};
        const float content_min{ImGui::GetWindowContentRegionMin().x};
        const float content_max{ImGui::GetWindowContentRegionMax().x};
        ImGui::SetCursorPos(
            ImVec2(content_min +
                       ((content_max - content_min - text_width) * 0.5F),
                   footer_y));
        text_disabled(version);
        if (ImGui::IsItemHovered()) {
            tooltip_text(std::format("{} {}", state.platform_name, arch));
        }
    }
    ImGui::End();
    return requests;
}

} // namespace wemod::gui::view
