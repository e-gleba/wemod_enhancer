#pragma once

// Single home for program identity and every shared constexpr the GUI
// and the CLI both read. Plain constexpr, no inline keyword:
// namespace-scope constexpr implies const, and const non-local
// variables have internal linkage, so including this header anywhere
// is safe with zero ODR risk.

#include <chrono>
#include <cstddef>
#include <string_view>

#ifndef WEMOD_ENHANCER_GUI_VERSION
#define WEMOD_ENHANCER_GUI_VERSION "0.0.0"
#endif

namespace wemod::gui
{
constexpr std::string_view program_name = "wemod_enhancer_gui";
constexpr std::string_view program_about = "WeMod Enhancer GUI and CLI";
constexpr std::string_view gui_version = WEMOD_ENHANCER_GUI_VERSION;
constexpr std::string_view patcher_name = "wemod_enhancer.py";
constexpr std::string_view installer_url =
    "https://api.wemod.com/client/download";
constexpr std::string_view launcher_clone_url =
    "https://github.com/DeckCheatz/wemod-launcher.git";
constexpr std::string_view launcher_guide_url =
    "https://github.com/DeckCheatz/wemod-launcher";
constexpr std::string_view issue_new_url =
    "https://github.com/e-gleba/wemod_enhancer/issues/new";
constexpr std::string_view releases_url =
    "https://github.com/e-gleba/wemod_enhancer/releases/latest";
constexpr std::string_view quickstart_url =
    "https://github.com/e-gleba/wemod_enhancer#quick-start";
constexpr std::string_view python_url = "https://www.python.org/downloads/";
constexpr std::size_t log_budget = 512UZ * 1024UZ;
constexpr std::size_t issue_log_budget = 3000UZ;
constexpr auto reprobe_interval = std::chrono::milliseconds{500};

#ifdef _WIN32
constexpr bool is_windows = true;
#else
constexpr bool is_windows = false;
#endif

constexpr std::string_view default_python =
    is_windows ? "python" : "python3";

static_assert(!program_name.empty());
static_assert(!gui_version.empty());
static_assert(!patcher_name.empty());
} // namespace wemod::gui
