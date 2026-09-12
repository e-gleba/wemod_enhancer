#pragma once

struct SDL_Renderer;
struct SDL_Window;

namespace wemod::gui::platform
{
struct context;

struct native_context final
{
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
};

[[nodiscard]] native_context native(const context& ctx) noexcept;
}
