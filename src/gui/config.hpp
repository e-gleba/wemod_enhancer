// Shared GUI constants. Single home for every magic value the UI and the
// platform layer both need, so a tweak never requires hunting call sites.
// SDL-free: this header must not include SDL or ImGui (see ui.cpp for the
// ImGui-side colors, which need ImVec4).
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wemod::gui
{

// GUI version: the CMake project version, injected at compile time - the GUI
// can never show a version that differs from its package.
inline constexpr std::string_view gui_version{WEMOD_ENHANCER_GUI_VERSION};

// One preprocessor site for the whole GUI. Everything else selects with
// `if constexpr (is_windows)` on this constant.
#ifdef _WIN32
inline constexpr bool is_windows{true};
#else
inline constexpr bool is_windows{false};
#endif

#if defined(__x86_64__) || defined(_M_X64)
inline constexpr std::string_view target_arch{"x86_64"};
#elif defined(__aarch64__) || defined(_M_ARM64)
inline constexpr std::string_view target_arch{"arm64"};
#else
inline constexpr std::string_view target_arch{"unknown"};
#endif

// What the current background command is, so poll_run() can react to
// completion: record the Python probe result, narrate the WeMod fetch,
// hint after a failed patch.
enum class run_kind : std::uint8_t { patcher, probe, wemod };

// Python probe tri-state: unknown / failed / works.
enum class probe_state : std::uint8_t { unknown, failed, works };

// CMake installs gui + script + dll into the same bindir.
inline constexpr std::string_view patcher_script_name{"wemod_enhancer.py"};
inline constexpr std::string_view version_dll_name{"version.dll"};

// "Download WeMod" endpoints. Windows: the official installer direct link
// (what https://www.wemod.com/download serves). Linux: the wemod-launcher
// repo + the DeckCheatz tutorial from the readme.
inline constexpr std::string_view wemod_installer_url{
    "https://api.wemod.com/client/download"};
inline constexpr std::string_view launcher_clone_url{
    "https://github.com/DaniAsh551/wemod-launcher.git"};
inline constexpr std::string_view launcher_repo_url{
    "https://deckcheatz.com/wemod-on-linux-full-guide/"};

// "Report bug" target; the log tail rides in the issue body.
inline constexpr std::string_view issue_new_url{
    "https://github.com/e-gleba/wemod_enhancer/issues/new"};
inline constexpr std::size_t issue_log_budget{3000};

// stat() is not free at 60 fps: edits re-probe immediately, this timer
// catches on-disk changes (e.g. the "Download WeMod" flow).
inline constexpr auto reprobe_interval{std::chrono::milliseconds(500)};

// Past this budget the oldest log lines are dropped at a newline.
inline constexpr std::size_t log_budget{512UZ * 1024UZ};

inline constexpr std::string_view default_python{is_windows
                                                     ? std::string_view{"python"}
                                                     : std::string_view{"python3"}};

// Fallback window size when the display cannot be queried. Units are SDL
// window coordinates. HiDPI is SDL_SetRenderScale, not a second multiply.
inline constexpr std::int32_t window_width_fallback{1024};
inline constexpr std::int32_t window_height_fallback{680};
inline constexpr std::int32_t window_min_width{880};
inline constexpr std::int32_t window_min_height{600};
inline constexpr std::int32_t window_max_width{1680};
inline constexpr std::int32_t window_max_height{1050};

} // namespace wemod::gui
