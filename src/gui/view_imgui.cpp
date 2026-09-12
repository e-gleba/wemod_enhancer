// view_imgui.cpp - Dear ImGui only. No SDL_Create*, no popen, no jobs.
// All SDL side effects go through backend:: (urls, dialogs,
// clipboard); all domain facts go through core::. This TU owns every
// widget and pixel of layout; backend/core never include imgui.

#include "app.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <format>
#include <string>

namespace wemod::gui::view
{

namespace
{

constexpr float button_padding{24.0F};
constexpr float section_indent{16.0F};
constexpr float row_height_scale{1.55F};

constexpr ImVec4 clear_color{0.10F, 0.10F, 0.12F, 1.00F};
constexpr ImVec4 color_ok{0.35F, 0.85F, 0.45F, 1.00F};
constexpr ImVec4 color_err{0.90F, 0.30F, 0.30F, 1.00F};
constexpr ImVec4 field_ok_bg{0.14F, 0.32F, 0.16F, 0.70F};
constexpr ImVec4 field_err_bg{0.32F, 0.14F, 0.14F, 0.70F};

void text_colored(const ImVec4& color, std::string_view text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopStyleColor();
}

void text_disabled(std::string_view text)
{
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void tooltip_text(std::string_view text)
{
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0F);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool action_button(const char* label, float width, float height)
{
    if (label == nullptr) {
        return false;
    }
    return ImGui::Button(label, ImVec2(width, height));
}

float equal_button_width(int count)
{
    if (count <= 0) {
        return 1.0F;
    }
    const ImGuiStyle& style{ImGui::GetStyle()};
    const float avail{ImGui::GetContentRegionAvail().x};
    const float gaps{style.ItemSpacing.x * static_cast<float>(count - 1)};
    return std::max((avail - gaps) / static_cast<float>(count), 1.0F);
}

void field_label(const char* label)
{
    if (label == nullptr) {
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

void help_marker(const char* text, const char* url, SDL_Window* parent)
{
    if (text == nullptr) {
        return;
    }
    ImGui::SameLine();
    text_disabled("(?)");
    if (ImGui::IsItemHovered()) {
        tooltip_text(text);
    }
    if (url != nullptr && ImGui::IsItemClicked()) {
        backend::open_url(url, parent);
    }
}

void push_field_tint(bool ok)
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ok ? field_ok_bg : field_err_bg);
}

void field_fail_hover(bool ok, const char* why)
{
    if (why == nullptr) {
        return;
    }
    if (!ok && ImGui::IsItemHovered()) {
        tooltip_text(why);
    }
}

void draw_settings(AppState& state)
{
    field_label("Patcher");
    help_marker("wemod_enhancer.py ships next to the executable. Re-download "
                "the GUI package if the field stays red.",
                "https://github.com/e-gleba/wemod_enhancer/releases/latest",
                state.window);
    push_field_tint(state.script_present);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##script_path",
                             "wemod_enhancer.py next to the exe",
                             &state.script_path);
    ImGui::PopStyleColor();
    field_fail_hover(state.script_present, "Patcher script not found");

    ImGui::Spacing();

    field_label("Python command");
    help_marker("The interpreter that runs the patcher. Default: python on "
                "Windows, python3 elsewhere. Point it at a full path if "
                "Python is not on PATH.",
                "https://www.python.org/downloads/", state.window);
    if (state.python_ok == ProbeState::works) {
        ImGui::SameLine();
        text_disabled(state.python_version);
    }
    const bool python_tinted{state.python_ok != ProbeState::unknown};
    const bool python_ok{state.python_ok == ProbeState::works};
    if (python_tinted) {
        push_field_tint(python_ok);
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##python", "python / python3", &state.python);
    if (python_tinted) {
        ImGui::PopStyleColor();
        field_fail_hover(python_ok, "Python failed to start");
    }

    ImGui::Spacing();

    field_label("version.dll");
    help_marker("The proxy DLL the patcher drops next to WeMod. Default: the "
                "copy next to the executable.",
                "https://github.com/e-gleba/wemod_enhancer#wemod-enhancer",
                state.window);
    push_field_tint(state.dll_present);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##version_dll", "version.dll next to the exe",
                             &state.version_dll);
    ImGui::PopStyleColor();
    field_fail_hover(state.dll_present, "version.dll not found");
}

} // namespace

const ImVec4& frame_clear_color() noexcept
{
    return clear_color;
}

void draw(AppState& state)
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

    core::probe_filesystem(state);
    const fs::path& resolved_dir{state.resolved_install_dir};
    const bool install_ok{!resolved_dir.empty()};
    const bool script_ok{state.script_present};

    field_label("WeMod folder");
    help_marker("The app-x.y.z folder with resources/app.asar inside. Pick the "
                "WeMod root and the newest version is used automatically. "
                "Linux: the wemod-launcher clone works too - after the first "
                "run + login its wemod_data/wemod_bin is picked up.",
                "https://github.com/e-gleba/wemod_enhancer#quick-start",
                state.window);

    const float browse_w{ImGui::CalcTextSize("Browse...").x +
                         (style.FramePadding.x * 2.0F) + button_padding};
    const float path_w{std::max(ImGui::GetContentRegionAvail().x - browse_w -
                                    style.ItemSpacing.x,
                                ImGui::GetFontSize() * 8.0F)};

    const bool folder_tinted{!state.install_dir.empty()};
    if (folder_tinted) {
        push_field_tint(install_ok);
    }
    ImGui::SetNextItemWidth(path_w);
    ImGui::InputTextWithHint("##install_dir", "path to WeMod",
                             &state.install_dir);
    if (folder_tinted) {
        ImGui::PopStyleColor();
        field_fail_hover(install_ok, "Not a WeMod install");
    }
    ImGui::SameLine();
    if (action_button("Browse...", browse_w, 0.0F)) {
        backend::show_folder_dialog(state);
    }

    if (install_ok && resolved_dir.string() != state.install_dir) {
        text_disabled(resolved_dir.string());
    }

    ImGui::Spacing();
    const int action_count{install_ok ? 2 : 3};
    const float action_w{equal_button_width(action_count)};

    const char* block_reason{core::run_block_reason(install_ok, script_ok)};
    const bool blocked{state.job.running() || block_reason != nullptr};
    ImGui::BeginDisabled(blocked);
    if (action_button("Patch", action_w, row_h)) {
        state.install_dir = resolved_dir.string();
        state.start_run("patch");
    }
    ImGui::EndDisabled();
    if (block_reason != nullptr &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip_text(block_reason);
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(blocked);
    if (action_button("Restore", action_w, row_h)) {
        state.start_run("restore");
    }
    ImGui::EndDisabled();
    if (block_reason != nullptr &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip_text(block_reason);
    }

    if (!install_ok) {
        ImGui::SameLine();
        if (action_button("Download WeMod", action_w, row_h)) {
            state.start_wemod_download();
        }
        if (ImGui::IsItemHovered()) {
            tooltip_text(is_windows
                             ? "Download the official WeMod installer into "
                               "your Downloads folder and run it"
                             : "Clone wemod-launcher into ~/wemod-launcher "
                               "and open the setup tutorial");
        }
    }

    ImGui::Spacing();
    if (state.job.running()) {
        text_disabled(core::running_status(state.kind));
    } else if (state.has_run) {
        if (state.last_exit_code == 0) {
            text_colored(color_ok, "Done. Launch WeMod - Pro is active.");
        } else {
            text_colored(color_err,
                         std::format("Failed (exit code {})",
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
    help_marker("Live stdout and stderr from the patcher, the Python probe, "
                "and Download WeMod. Copy it below if something fails.",
                "https://github.com/e-gleba/wemod_enhancer/issues/new",
                state.window);

    const float line_height{ImGui::GetTextLineHeightWithSpacing()};
    const float toolbar_h{row_h + line_height + (style.ItemSpacing.y * 3.0F)};
    const float log_height{std::max(ImGui::GetContentRegionAvail().y - toolbar_h,
                                    line_height * 4.0F)};

    ImGui::BeginChild("##log", ImVec2(0.0F, log_height),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (state.log.empty()) {
        text_disabled("Patch output appears here. Copy it with the button below "
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

    ImGui::Spacing();
    const float util_w{equal_button_width(3)};
    if (action_button("Copy output", util_w, row_h)) {
        core::copy_output(state);
    }
    if (ImGui::IsItemHovered()) {
        tooltip_text("Copy the log and environment info to the clipboard");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.log.empty());
    if (action_button("Clear output", util_w, row_h)) {
        core::clear_output(state);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip_text("Clear the log");
    }
    ImGui::SameLine();
    if (action_button("Report bug", util_w, row_h)) {
        core::report_bug(state);
    }
    if (ImGui::IsItemHovered()) {
        tooltip_text("Open a pre-filled GitHub issue with the log attached");
    }

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
            tooltip_text(std::string(backend::platform_name()) + " " +
                         std::string(target_arch));
        }
    }

    ImGui::End();
}

} // namespace wemod::gui::view
