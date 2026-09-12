// view.hpp - Passive View contract. Knows model.hpp (Snapshot only)
// and <string_view>; knows nothing about SDL, the Model, the
// presenter, or logging. view_imgui.cpp is the only implementation.
// The presenter builds a Snapshot, calls draw(), then applies the
// returned Intent. View never mutates the Model directly.

#pragma once

#include <string_view>

namespace wemod::gui
{

namespace view
{

// Read-only frame data: everything the widgets need, nothing more.
struct Snapshot final
{
    std::string_view install_dir;
    std::string_view script_path;
    std::string_view python;
    std::string_view version_dll;
    std::string_view log;
    std::string_view resolved_dir;
    std::string_view python_version;
    bool install_ok{false};
    bool script_ok{false};
    bool dll_present{false};
    bool job_running{false};
    bool has_run{false};
    bool log_empty{true};
    bool quit_requested{false};
    std::int32_t last_exit_code{0};
    float copied_flash{0.0F};
    int python_state{0}; // 0 unknown, 1 failed, 2 works
    int run_kind{0};     // mirrors RunKind for the status line
    std::string_view gui_version;
    std::string_view target_arch;
};

// One user action per frame at most; the presenter applies it.
struct Intent final
{
    enum class Type : std::uint8_t
    {
        none,
        patch,
        restore,
        download_wemod,
        browse,
        copy_output,
        clear_output,
        report_bug,
    } type{Type::none};
};

// Draw one frame from snap (may edit text fields in place via the
// passed mutable refs - the only view-side mutation allowed).
// Returns the single user intent for the presenter to apply.
[[nodiscard]] Intent draw(const Snapshot& snap,
                          std::string& install_dir,
                          std::string& script_path,
                          std::string& python,
                          std::string& version_dll,
                          float delta_seconds);

} // namespace view

} // namespace wemod::gui
