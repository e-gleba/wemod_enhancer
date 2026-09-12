// SDL3 platform layer: every OS touchpoint lives here, checked and loud.
// ImGui never calls SDL directly; ui.cpp talks to this TU through state.
// Errors surface via SDL_ShowSimpleMessageBox (fatal, with a window when we
// have one) plus SDL_Log - no silent failure, no hidden errno.
//
// SDL3 API refs (checked against SDL 3.4.x headers, bundled release-3.4.14):
//   SDL_CreateProcessWithProperties / SDL_ReadProcess / SDL_WaitProcess /
//   SDL_DestroyProcess - SDL_process.h, CategoryProcess; stdout+stderr both
//   set to SDL_PROCESS_STDIO_APP so one SDL_ReadProcess captures everything
//   with no pipe-drain thread.
//   SDL_ShowSimpleMessageBox - SDL_messagebox.h, CategoryMessagebox; callable
//   before SDL_Init, falls back to SDL_Log when video is unavailable.
//   SDL_EnumerateDirectory / SDL_GetPathInfo - SDL_filesystem.h,
//   CategoryFilesystem: native directory scan, no std::filesystem walk.
//   SDL_GetBasePath / SDL_GetUserFolder / SDL_GetEnvironmentVariable -
//   SDL_filesystem.h / SDL_stdinc.h: exe anchor, Downloads dir, env.
//   SDL_SetClipboardText / SDL_OpenURL / SDL_GetPlatform - SDL_clipboard.h /
//   SDL_misc.h / SDL_platform.h.
//   SDL_CreateWindowAndRenderer / SDL_SetRenderVSync /
//   SDL_GetDisplayUsableBounds / SDL_SetRenderScale / SDL_RenderClear /
//   SDL_RenderPresent - SDL_render.h / SDL_video.h: window + HiDPI knob.
//   SDL_ShowOpenFolderDialog - SDL_dialog.h: native folder picker.
#pragma once

#include "config.hpp"
#include "runner.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wemod::gui
{

namespace fs = std::filesystem;

// Fatal-error surface: native assert window + SDL_Log. Safe before
// SDL_Init and on any thread; falls back to SDL_Log when video is down.
void fatal_message(const char* title, const std::string& message) noexcept;

// Non-fatal note: SDL_Log only, never a popup.
void log_error(const std::string& message) noexcept;

// Run a child via SDL_CreateProcessWithProperties, capture merged
// stdout+stderr via SDL_ReadProcess, wait via SDL_WaitProcess. Argv form:
// no shell, no quoting, no popen, no 2>&1 hack on any platform.
// `args[0]` is the program; remaining entries are its arguments.
// Empty output + exit -1 when the child cannot be spawned.
[[nodiscard]] run_result run_process(const std::vector<std::string>& args);

// Draft of `run_process` for call sites that already hold C strings.
// Same semantics, no copy beyond the argv table.
[[nodiscard]] run_result run_process_argv(
    const std::vector<const char*>& args);

// Native directory scan for app-x.y.z via SDL_EnumerateDirectory +
// SDL_GetPathInfo. Returns the newest entry, empty path when none.
// Lexicographic order would put app-10 before app-9, so numeric compare.
[[nodiscard]] fs::path newest_app_dir(const fs::path& root);

// "app-10.2.3" -> {10, 2, 3}; non-numeric tokens become 0. Pure.
[[nodiscard]] std::vector<std::int32_t> version_parts(std::string_view name);

// WeMod's default install dir for the current platform, or empty.
[[nodiscard]] std::string default_install_dir();

// Dir the running exe sits in (SDL_GetBasePath). Single anchor: the
// patcher ships next to the exe, so the install stays one movable folder.
[[nodiscard]] fs::path exe_dir();

// OS Downloads folder (SDL_GetUserFolder) / home dir (HOME env).
// Empty + logged on failure. Keeps SDL env lookups behind this layer
// so state.cpp never calls SDL directly.
[[nodiscard]] std::string downloads_dir();
[[nodiscard]] std::string home_dir();

// URL percent-encoding (RFC 3986, unreserved pass through) for the
// pre-filled GitHub issue behind Report bug. Pure.
[[nodiscard]] std::string url_encode(std::string_view text);

// Native folder picker. Synchronous wrapper: shows the SDL dialog and
// returns the chosen path, or empty when cancelled/unavailable.
[[nodiscard]] std::string pick_folder(void* window_handle,
                                      const std::string& current);

// Clipboard + browser + platform string. All checked; failures log.
bool set_clipboard_text(const std::string& text);
bool open_url(const char* url) noexcept;
[[nodiscard]] std::string platform_name();

// Comfortable window size in SDL window coordinates: a fraction of the
// usable display, clamped so laptops stay usable and 4K stays sane.
// HiDPI is SDL_SetRenderScale, applied by app.cpp every frame.
[[nodiscard]] std::pair<std::int32_t, std::int32_t> pick_window_size();

// Thin window+renderer handle pair owned by app.cpp.
struct window final
{
    SDL_Window* handle{nullptr};
    SDL_Renderer* renderer{nullptr};

    window() = default;
    window(const window&) = delete;
    window& operator=(const window&) = delete;
    window(window&& other) noexcept
        : handle{std::exchange(other.handle, nullptr)}
        , renderer{std::exchange(other.renderer, nullptr)}
    {
    }
    window& operator=(window&& other) noexcept
    {
        if (this != &other) {
            close();
            handle = std::exchange(other.handle, nullptr);
            renderer = std::exchange(other.renderer, nullptr);
        }
        return *this;
    }
    ~window()
    {
        close();
    }
    void close() noexcept
    {
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }
        if (handle != nullptr) {
            SDL_DestroyWindow(handle);
            handle = nullptr;
        }
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return handle != nullptr && renderer != nullptr;
    }
};

// Create window+renderer or show the fatal box and return empty.
// Fatal here is correct: without a window there is no app.
[[nodiscard]] window make_window();

// Per-frame present helpers, all checked: scale (the one HiDPI knob),
// clear with the theme color, present. False keeps the frame loop alive
// but logs - a single failed present is not fatal.
bool begin_frame(SDL_Renderer* renderer, float scale_x, float scale_y,
                 float r, float g, float b, float a);
bool present_frame(SDL_Renderer* renderer);

} // namespace wemod::gui
