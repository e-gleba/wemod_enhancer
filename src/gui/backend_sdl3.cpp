// backend_sdl3.cpp - SDL3 platform layer. No ImGui, no domain logic.
//
// Every SDL call with a failure return is checked at the call site via
// check(): failures log through SDL_Log and, when fatal or parented,
// also pop an SDL message box so a broken video driver, missing
// Downloads folder, or clipboard/URL failure is visible instead of
// a silent exit. All entry points are noexcept: SDL failures are values
// (false / empty), never exceptions.
//
// Folder dialog handoff: SDL may invoke the dialog callback on an OS
// thread and after AppState is gone, so the callback never touches
// AppState. It stages the picked path in a mutex-guarded slot that
// the UI thread harvests via take_pending_folder().

#include "app.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <string>

namespace wemod::gui::backend
{

namespace
{

std::mutex folder_mutex;
std::optional<std::string> pending_folder;

// May run on an OS thread and may outlive AppState: copy the path
// under a lock, touch nothing else. userdata is always nullptr.
void SDLCALL on_folder_chosen(void* userdata,
                              const char* const* filelist,
                              int /*filter*/)
{
    (void)userdata;
    if (filelist == nullptr || filelist[0] == nullptr) {
        return;
    }
    try {
        const std::lock_guard<std::mutex> lock{folder_mutex};
        pending_folder = filelist[0];
    } catch (...) {
    }
}

} // namespace

void log_error(const std::string& message) noexcept
{
    SDL_Log("%s", message.c_str()); // NOLINT(cppcoreguidelines-pro-type-vararg)
}

void show_error(const char* title,
                const std::string& message,
                SDL_Window* parent) noexcept
{
    log_error(message);
    if (title == nullptr) {
        return;
    }
    // A failing box only logs - never throws, never recurses.
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title,
                                  message.c_str(), parent)) {
        log_error(std::string("SDL_ShowSimpleMessageBox: ") + SDL_GetError());
    }
}

bool check(bool ok,
           const char* what,
           SDL_Window* parent,
           bool fatal) noexcept
{
    if (ok) {
        return true;
    }
    const char* what_safe{what != nullptr ? what : "SDL"};
    std::string message;
    try {
        message = std::string(what_safe) + ": " + SDL_GetError();
    } catch (...) {
        return false;
    }
    if (fatal || parent != nullptr) {
        show_error(what_safe, message, parent);
    } else {
        log_error(message);
    }
    return false;
}

const char* env_var(const char* name) noexcept
{
    if (name == nullptr) {
        return nullptr;
    }
    return SDL_GetEnvironmentVariable(SDL_GetEnvironment(), name);
}

fs::path exe_dir() noexcept
{
    try {
        if (const char* base{SDL_GetBasePath()}) {
            return {base};
        }
        return fs::temp_directory_path() / "wemod_enhancer";
    } catch (...) {
        return {};
    }
}

std::string platform_name() noexcept
{
    try {
        if (const char* name{SDL_GetPlatform()}) {
            return {name};
        }
    } catch (...) {
    }
    return "unknown";
}

fs::path downloads_dir() noexcept
{
    try {
        // SDL-owned memory - do not free.
        if (const char* dir{SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)}) {
            return {dir};
        }
        log_error(std::string("SDL_GetUserFolder: ") + SDL_GetError());
    } catch (...) {
    }
    return {};
}

bool open_url(const char* url, SDL_Window* parent) noexcept
{
    if (url == nullptr) {
        return false;
    }
    // SDL3: bool SDL_OpenURL (true = success).
    return check(SDL_OpenURL(url), "SDL_OpenURL", parent, false);
}

bool set_clipboard(const std::string& text, SDL_Window* parent) noexcept
{
    // SDL3: bool SDL_SetClipboardText (true = success).
    return check(SDL_SetClipboardText(text.c_str()),
                 "SDL_SetClipboardText", parent, false);
}

void show_folder_dialog(AppState& state) noexcept
{
    const char* current{state.install_dir.empty() ? nullptr
                                                  : state.install_dir.c_str()};
    // nullptr userdata: the callback stages the path in pending_folder
    // and never touches AppState, so a dialog outliving shutdown is safe.
    SDL_ShowOpenFolderDialog(on_folder_chosen, nullptr, state.window,
                             current, false);
}

bool take_pending_folder(std::string& out) noexcept
{
    try {
        const std::lock_guard<std::mutex> lock{folder_mutex};
        if (!pending_folder.has_value()) {
            return false;
        }
        out = std::move(*pending_folder);
        pending_folder.reset();
        return true;
    } catch (...) {
        return false;
    }
}

WindowSize pick_window_size() noexcept
{
    try {
        SDL_Rect usable{};
        if (!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(),
                                        &usable) ||
            usable.w <= 0 || usable.h <= 0) {
            return {};
        }
        WindowSize out{};
        out.width = std::clamp(usable.w / 2, window_min_width,
                               window_max_width);
        out.height = std::clamp((usable.h * 3) / 5, window_min_height,
                                window_max_height);
        return out;
    } catch (...) {
        return {};
    }
}

} // namespace wemod::gui::backend
