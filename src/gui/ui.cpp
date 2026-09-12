// Dear ImGui view implementation. Widgets only: no popen, no env, no
// filesystem walks beyond the throttled probe_filesystem() invariant.
#include "ui.hpp"

#include "config.hpp"
#include "platform.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <gsl/assert>

#include <algorithm>
#include <cfloat>
#include <format>
#include <string>

namespace wemod::gui
{

namespace
{

// Extra horizontal padding on the Browse button so it matches the
// path field's visual weight. Action buttons ignore this - they
// stretch to share the full row.
constexpr float button_padding{24.0F};
constexpr float section_indent{16.0F};

// One height for every full-span button row (Patch / Restore and
// Copy / Clear / Report). Same scale = one control language.
// Font-size derived via GetFrameHeight().
constexpr float row_height_scale{1.55F};

// Status colors (replacing the magic-number literals).
constexpr ImVec4 color_ok{0.35F, 0.85F, 0.45F, 1.00F};
constexpr ImVec4 color_err{0.90F, 0.30F, 0.30F, 1.00F};

// Quiet fill tints for path fields. Validity is the color, not a
// word next to the label.
constexpr ImVec4 field_ok_bg{0.14F, 0.32F, 0.16F, 0.70F};
constexpr ImVec4 field_err_bg{0.32F, 0.14F, 0.14F, 0.70F};

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
// Single string_view overload: string/const char* both convert,
// so no ambiguity at call sites.
void tooltip_text(const std::string_view text)
{
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0F);
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

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
        tooltip_text(std::string_view{text});
    }
    if (ImGui::IsItemClicked()) {
        open_url(url);
    }
}

// Quiet green / red fill on the next input. Validity is the color.
void push_field_tint(const bool ok)
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ok ? field_ok_bg : field_err_bg);
}

// Hover on a red field: one line, no extra label clutter.
void field_fail_hover(const bool ok, const char* why)
{
    Expects(why != nullptr);
    if (!ok && ImGui::IsItemHovered()) {
        tooltip_text(std::string_view{why});
    }
}

// Why Patch / Restore is disabled, or nullptr when it can run.
[[nodiscard]] const char* run_block_reason(const bool install_ok,
                                           const bool script_ok)
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

// Status line text while a background command runs.
[[nodiscard]] std::string_view running_status(const run_kind kind) noexcept
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

// Settings body: patcher / Python / version.dll as tinted path fields.
void draw_settings(app_state& state)
{
    field_label("Patcher");
    help_marker(
        "wemod_enhancer.py ships next to the executable. Re-download "
        "the GUI package if the field stays red.",
        "https://github.com/e-gleba/wemod_enhancer/releases/latest");
    push_field_tint(state.script_present);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##script_path", "wemod_enhancer.py next to the exe",
                             &state.script_path);
    ImGui::PopStyleColor();
    field_fail_hover(state.script_present, "Patcher script not found");

    ImGui::Spacing();

    field_label("Python command");
    help_marker(
        "The interpreter that runs the patcher. Default: python on "
        "Windows, python3 elsewhere. Point it at a full path if "
        "Python is not on PATH.",
        "https://www.python.org/downloads/");
    // Version rides next to the label, not the field. An unconditional
    // SameLine put the input on the label row (narrow, shifted) while
    // Patcher / version.dll stayed full-width below their labels.
    if (state.python_ok == probe_state::works) {
        ImGui::SameLine();
        text_disabled(state.python_version);
    }
    const bool python_tinted{state.python_ok != probe_state::unknown};
    const bool python_ok{state.python_ok == probe_state::works};
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
    help_marker(
        "The proxy DLL the patcher drops next to WeMod. Default: the "
        "copy next to the executable.",
        "https://github.com/e-gleba/wemod_enhancer#wemod-enhancer");
    push_field_tint(state.dll_present);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##version_dll", "version.dll next to the exe",
                             &state.version_dll);
    ImGui::PopStyleColor();
    field_fail_hover(state.dll_present, "version.dll not found");
}

} // namespace

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
    field_label("WeMod folder");
    help_marker(
        "The app-x.y.z folder with resources/app.asar inside. Pick the "
        "WeMod root and the newest version is used automatically. "
        "Linux: the wemod-launcher clone works too - after the first "
        "run + login its wemod_data/wemod_bin is picked up.",
        "https://github.com/e-gleba/wemod_enhancer#quick-start");

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
        if (std::string picked{pick_folder(state.window_handle, state.install_dir)};
            !picked.empty()) {
            state.install_dir = std::move(picked);
        }
    }

    if (install_ok && resolved_dir.string() != state.install_dir) {
        text_disabled(resolved_dir.string());
    }

    // --- Actions: equal-width row, full span under the path -----------
    ImGui::Spacing();
    const int action_count{install_ok ? 2 : 3};
    const float action_w{equal_button_width(action_count)};

    const char* block_reason{run_block_reason(install_ok, script_ok)};
    const bool busy{state.jobs.running()};
    const bool blocked{busy || block_reason != nullptr};
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
        tooltip_text(std::string_view{block_reason});
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(blocked);
    if (action_button("Restore", action_w, row_h)) {
        start_run(state, "restore");
    }
    ImGui::EndDisabled();
    if (block_reason != nullptr &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        tooltip_text(std::string_view{block_reason});
    }

    if (!install_ok) {
        ImGui::SameLine();
        ImGui::BeginDisabled(busy);
        if (action_button("Download WeMod", action_w, row_h)) {
            start_wemod_download(state);
        }
        ImGui::EndDisabled();
        if (!busy && ImGui::IsItemHovered()) {
            tooltip_text(is_windows ? std::string_view{
                                          "Download the official WeMod installer into "
                                          "your Downloads folder and run it"}
                                      : std::string_view{
                                            "Clone wemod-launcher into ~/wemod-launcher "
                                            "and open the setup tutorial"});
        }
    }

    // --- Status -------------------------------------------------------
    ImGui::Spacing();
    if (busy) {
        text_disabled(running_status(state.kind));
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

    // --- Settings (collapsed by default), full window width -----------
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Settings")) {
        ImGui::Indent(section_indent);
        draw_settings(state);
        ImGui::Unindent(section_indent);
    }

    // --- Console: live stdout/stderr, fills the rest of the window ----
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    field_label("Console");
    help_marker(
        "Live stdout and stderr from the patcher, the Python probe, "
        "and Download WeMod. Copy it below if something fails.",
        "https://github.com/e-gleba/wemod_enhancer/issues/new");

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

    // Footer: current APIs only (content-region Min/Max are obsolete in
    // the pinned imgui and break under IMGUI_DISABLE_OBSOLETE_FUNCTIONS).
    // Origin + span are captured before Copied! moves the cursor.
    ImGui::Spacing();
    const ImVec2 content_start{ImGui::GetCursorScreenPos()};
    const float content_span{ImGui::GetContentRegionAvail().x};
    if (state.copied_flash > 0.0F) {
        state.copied_flash -= ImGui::GetIO().DeltaTime;
        text_colored(color_ok, "Copied!");
    }
    {
        const std::string version_text{std::format("v{}", gui_version)};
        const float text_width{ImGui::CalcTextSize(version_text.c_str()).x};
        const float version_x{content_start.x +
                              ((content_span - text_width) * 0.5F)};
        ImGui::SetCursorScreenPos(ImVec2(version_x, content_start.y));
        ImGui::TextUnformatted(version_text.c_str());
        if (ImGui::IsItemHovered()) {
            tooltip_text(platform_name() + " " + std::string(target_arch));
        }
    }

    ImGui::End();
}

} // namespace wemod::gui
