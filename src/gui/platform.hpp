// platform.hpp - OS services contract. Knows nothing about log.hpp
// sinks, model state, or ImGui. backend_sdl3.cpp is the only
// implementation; swapping SDL means rewriting one file.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Opaque window handle: SDL_Window* in the backend, void* everywhere
// else. No SDL headers leak through this interface.
struct PlatformWindow;

namespace wemod::gui
{

namespace fs = std::filesystem;

namespace platform
{

struct WindowSize final
{
    std::int32_t width{1024};
    std::int32_t height{680};
};

// The last backend error text (SDL_GetError snapshot). Empty = none.
// Thread-safe enough: written by backend, read on UI thread.
[[nodiscard]] std::string last_error() noexcept;
[[nodiscard]] const char* env_var(const char* name) noexcept;
[[nodiscard]] fs::path exe_dir() noexcept;
[[nodiscard]] std::string platform_name() noexcept;
// Empty path when the folder is unknown (caller picks the fallback).
[[nodiscard]] fs::path downloads_dir() noexcept;
// True on success; failure records last_error() (never throws).
bool open_url(std::string_view url, PlatformWindow* parent) noexcept;
bool set_clipboard(std::string_view text,
                   PlatformWindow* parent) noexcept;
void show_folder_dialog(PlatformWindow* parent,
                        std::string_view current) noexcept;
// Harvest the staged dialog path on the UI thread. True = out set.
bool take_pending_folder(std::string& out) noexcept;
[[nodiscard]] WindowSize pick_window_size() noexcept;

} // namespace platform

} // namespace wemod::gui
