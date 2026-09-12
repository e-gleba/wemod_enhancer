// backend_sdl3.cpp - the ONLY file implementing platform.hpp.
// Talks SDL3 + log:: only. Knows nothing about Model, view, or the
// presenter. Errors are logged; failures are values, never throws.

#include "platform.hpp"

#include "log.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <mutex>
#include <optional>
#include <string>

struct PlatformWindow
{
    SDL_Window* handle{nullptr};
};

namespace wemod::gui::platform
{

namespace
{

std::mutex state_mutex;
std::string last_error_text;
std::mutex folder_mutex;
std::optional<std::string> pending_folder;

void record(std::string_view what) noexcept
{
    std::string message;
    try {
        const char* err{SDL_GetError()};
        message = std::string(what) + ": " + (err ? err : "?");
    } catch (...) {
        return;
    }
    try {
        const std::lock_guard<std::mutex> lock{state_mutex};
        last_error_text = message;
    } catch (...) {
    }
    log::error(message);
}

// May run on an OS thread and may outlive the app: stage the path
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

[[nodiscard]] SDL_Window* to_sdl(PlatformWindow* window) noexcept
{
    return window != nullptr ? window->handle : nullptr;
}

} // namespace

std::string last_error() noexcept
{
    try {
        const std::lock_guard<std::mutex> lock{state_mutex};
        return last_error_text;
    } catch (...) {
        return {};
    }
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
        record("SDL_GetUserFolder");
    } catch (...) {
    }
    return {};
}

bool open_url(std::string_view url, PlatformWindow* parent) noexcept
{
    if (url.empty()) {
        return false;
    }
    // SDL3 bool API: true = success.
    std::string owned;
    try {
        owned = std::string(url);
    } catch (...) {
        return false;
    }
    if (SDL_OpenURL(owned.c_str())) {
        return true;
    }
    record("SDL_OpenURL");
    return false;
}

bool set_clipboard(std::string_view text, PlatformWindow* parent) noexcept
{
    std::string owned;
    try {
        owned = std::string(text);
    } catch (...) {
        return false;
    }
    if (SDL_SetClipboardText(owned.c_str())) {
        return true;
    }
    record("SDL_SetClipboardText");
    return false;
}

void show_folder_dialog(PlatformWindow* parent,
                        std::string_view current) noexcept
{
    std::string owned;
    try {
        owned = std::string(current);
    } catch (...) {
        return;
    }
    const char* arg{owned.empty() ? nullptr : owned.c_str()};
    // nullptr userdata: the callback stages into pending_folder and
    // never touches app state, so a dialog outliving shutdown is safe.
    SDL_ShowOpenFolderDialog(on_folder_chosen, nullptr, to_sdl(parent),
                             arg, false);
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

} // namespace wemod::gui::platform
