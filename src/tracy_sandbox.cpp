#include <cmath>
#include <cstring>

#include <array>
#include <atomic>
#include <cstdlib>
#include <format>
#include <memory>
#include <numbers>
#include <print>
#include <thread>
#include <vector>

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <gsl/gsl>
#include <tracy/Tracy.hpp>

// ── Tracy-tracked mutex ──────────────────────────────────────────────
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
TracyLockable(std::mutex, g_data_mutex);
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// ── Tracy-tracked allocator ──────────────────────────────────────────
template <typename type_t> struct tracked_allocator
{
    using value_type = type_t;

    tracked_allocator() = default;
    template <typename other_t>
    constexpr tracked_allocator(
        const tracked_allocator<other_t>& /*unused*/) noexcept
    {
    }

    [[nodiscard]] type_t* allocate(std::size_t count)
    {
        const auto bytes = count * sizeof(type_t);
        auto*      ptr   = static_cast<type_t*>(::operator new(bytes));
        TracyAllocN(ptr, bytes, "tracked");
        return ptr;
    }

    void deallocate(type_t* ptr, std::size_t count) noexcept
    {
        TracyFreeN(ptr, "tracked");
        ::operator delete(ptr, count * sizeof(type_t));
    }
};

using tracked_vector = std::vector<std::byte, tracked_allocator<std::byte>>;

namespace {

// ── Catppuccin Mocha palette ─────────────────────────────────────────
struct rgba
{
    std::uint8_t r, g, b, a;
};

constexpr rgba k_base     = { .r = 30, .g = 30, .b = 46, .a = 255 };
constexpr rgba k_surface0 = { .r = 49, .g = 50, .b = 68, .a = 255 };
constexpr rgba k_green    = { .r = 166, .g = 227, .b = 161, .a = 255 };
constexpr rgba k_mauve    = { .r = 203, .g = 166, .b = 247, .a = 255 };
constexpr rgba k_peach    = { .r = 250, .g = 179, .b = 135, .a = 255 };
constexpr rgba k_sky      = { .r = 137, .g = 220, .b = 235, .a = 255 };
constexpr rgba k_pink     = { .r = 245, .g = 194, .b = 231, .a = 255 };
constexpr rgba k_red      = { .r = 243, .g = 139, .b = 168, .a = 255 };
constexpr rgba k_yellow   = { .r = 249, .g = 226, .b = 175, .a = 255 };

constexpr std::array k_orbit_colors = { k_green, k_mauve, k_peach, k_sky,
                                        k_pink,  k_red,   k_yellow };

void set_color(SDL_Renderer* r, rgba c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

// ── Render helpers ───────────────────────────────────────────────────
void draw_filled_circle(SDL_Renderer* renderer,
                        float         cx,
                        float         cy,
                        float         radius)
{
    ZoneScopedN("draw_filled_circle");
    const int r  = static_cast<int>(radius);
    const int r2 = r * r;
    for (int dy = -r; dy <= r; ++dy) {
        const int half_w =
            static_cast<int>(std::sqrt(static_cast<float>(r2 - (dy * dy))));
        const SDL_FRect row = {
            .x = cx - static_cast<float>(half_w),
            .y = cy + static_cast<float>(dy),
            .w = static_cast<float>(half_w * 2),
            .h = 1.0F,
        };
        SDL_RenderFillRect(renderer, &row);
    }
}

void draw_scene(SDL_Renderer* renderer, std::uint64_t frame)
{
    ZoneScopedN("draw_scene");

    constexpr float win_w    = 1280.0F;
    constexpr float win_h    = 720.0F;
    constexpr float center_x = win_w / 2.0F;
    constexpr float center_y = win_h / 2.0F;

    const auto t = static_cast<float>(frame) / 60.0F;

    // Background
    set_color(renderer, k_base);
    SDL_RenderClear(renderer);

    // Pulsing grid
    {
        ZoneScopedN("grid");
        const auto pulse = 0.3F + (0.15F * std::sin(t * 1.5F));
        const auto alpha = static_cast<std::uint8_t>(pulse * 255.0F);
        SDL_SetRenderDrawColor(
            renderer, k_surface0.r, k_surface0.g, k_surface0.b, alpha);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        constexpr float spacing = 40.0F;
        for (float x = 0.0F; x < win_w; x += spacing) {
            SDL_RenderLine(renderer, x, 0.0F, x, win_h);
        }
        for (float y = 0.0F; y < win_h; y += spacing) {
            SDL_RenderLine(renderer, 0.0F, y, win_w, y);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    // Orbiting circles
    {
        ZoneScopedN("orbits");
        for (std::size_t i = 0; i < k_orbit_colors.size(); ++i) {
            const auto fi    = static_cast<float>(i);
            const auto angle = (t * (0.8F + (fi * 0.15F))) +
                               ((fi * std::numbers::pi_v<float> * 2.0F) / 7.0F);
            const auto orbit  = 100.0F + (fi * 30.0F);
            const auto radius = 8.0F + (4.0F * std::sin((t * 2.0F) + fi));
            const auto cx     = center_x + (orbit * std::cos(angle));
            const auto cy     = center_y + (orbit * std::sin(angle));

            set_color(renderer, k_orbit_colors.at(i));
            draw_filled_circle(renderer, cx, cy, radius);
        }
    }

    // Central pulsing circle
    {
        ZoneScopedN("center_pulse");
        const auto radius = 30.0F + (10.0F * std::sin(t * 3.0F));
        set_color(renderer, k_mauve);
        draw_filled_circle(renderer, center_x, center_y, radius);
    }

    // Bouncing bars — one per worker thread
    {
        ZoneScopedN("worker_bars");
        constexpr float      bar_w      = 60.0F;
        constexpr float      gap        = 20.0F;
        constexpr float      total      = (4.0F * bar_w) + (3.0F * gap);
        const float          start      = (win_w - total) / 2.0F;
        constexpr std::array bar_colors = { k_green, k_peach, k_sky, k_pink };

        for (int i = 0; i < 4; ++i) {
            const auto fi = static_cast<float>(i);
            const auto height =
                40.0F + (30.0F * std::abs(std::sin((t * 2.5F) + (fi * 1.2F))));
            const SDL_FRect bar = {
                .x = start + (fi * (bar_w + gap)),
                .y = (win_h - height) - 20.0F,
                .w = bar_w,
                .h = height,
            };
            set_color(renderer, bar_colors.at(static_cast<std::size_t>(i)));
            SDL_RenderFillRect(renderer, &bar);
        }
    }
}

// ── Frame image capture for Tracy ────────────────────────────────────
void capture_frame_image(SDL_Renderer* renderer)
{
    ZoneScopedN("capture_frame_image");

    constexpr int capture_w = 320;
    constexpr int capture_h = 180;

    SDL_Surface* full = SDL_RenderReadPixels(renderer, nullptr);
    if (full == nullptr) {
        return;
    }
    auto full_guard = gsl::finally([&] { SDL_DestroySurface(full); });

    SDL_Surface* scaled =
        SDL_CreateSurface(capture_w, capture_h, SDL_PIXELFORMAT_RGBA32);
    if (scaled == nullptr) {
        return;
    }
    auto scaled_guard = gsl::finally([&] { SDL_DestroySurface(scaled); });

    if (!SDL_BlitSurfaceScaled(
            full, nullptr, scaled, nullptr, SDL_SCALEMODE_LINEAR)) {
        return;
    }

    if (!SDL_LockSurface(scaled)) {
        return;
    }
    auto lock_guard = gsl::finally([&] { SDL_UnlockSurface(scaled); });

    FrameImage(scaled->pixels, capture_w, capture_h, 0, false);
}

// ── Worker: simulates CPU load ───────────────────────────────────────
void worker_thread(std::size_t id, std::atomic<bool>& running)
{
    const auto name = std::format("worker_{}", id);
    tracy::SetThreadName(name.c_str());

    while (running.load(std::memory_order_relaxed)) {
        ZoneScopedN("worker_tick");

        {
            ZoneScopedN("alloc_churn");
            tracked_vector buf(1024 * (1 + id));
            std::memset(buf.data(), gsl::narrow_cast<int>(id), buf.size());
        }

        {
            ZoneScopedN("contended_lock");
            std::scoped_lock lock(g_data_mutex);
            volatile int     sink = 0;
            for (int i = 0; i < 10000; ++i) {
                sink += i;
            }
            (void)sink;
        }

        TracyPlot(name.c_str(), static_cast<double>(id * 100));
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        FrameMarkNamed("worker");
    }
}

} // namespace

// ── App state ────────────────────────────────────────────────────────

struct app_state final
{
    SDL_Window*                 window   = nullptr;
    SDL_Renderer*               renderer = nullptr;
    std::atomic<bool>           running{ true };
    std::array<std::jthread, 4> workers;
    std::uint64_t               frame_count = 0;
};

// NOLINTBEGIN(readability-identifier-naming)

SDL_AppResult SDL_AppInit(void**                 appstate,
                          [[maybe_unused]] int   argc,
                          [[maybe_unused]] char* argv[])
{ // NOLINT(cppcoreguidelines-avoid-c-arrays)
    ZoneScopedN("app_init");
    TracySetProgramName("tracy_sandbox");
    TracyMessageL("app_init => start");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println(stderr, "sdl_init failed: {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto owner = std::make_unique<app_state>();

    owner->window =
        SDL_CreateWindow("Tracy Sandbox", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (owner->window == nullptr) {
        std::println(stderr, "create_window failed: {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Enable VSync on the renderer — caps to display refresh rate,
    // gives Tracy time to ingest each frame image before the next arrives.
    owner->renderer = SDL_CreateRenderer(owner->window, nullptr);
    if (owner->renderer == nullptr) {
        std::println(stderr, "create_renderer failed: {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!SDL_SetRenderVSync(owner->renderer, 1)) {
        // Non-fatal: worst case we run uncapped
        std::println(
            stderr, "vsync failed: {}, running uncapped", SDL_GetError());
    }

    for (std::size_t i = 0; i < owner->workers.size(); ++i) {
        owner->workers.at(i) = std::jthread(
            [i, &running = owner->running] { worker_thread(i, running); });
    }

    *appstate = owner.release();
    TracyMessageL("app_init => done");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent([[maybe_unused]] void* appstate, SDL_Event* event)
{
    ZoneScopedN("app_event");

    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE) {
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    ZoneScopedN("app_iterate");
    auto* state = static_cast<app_state*>(appstate);

    {
        ZoneScopedN("main_alloc");
        tracked_vector frame_data(4096);
        std::memset(frame_data.data(), 0xAB, frame_data.size());
    }

    draw_scene(state->renderer, state->frame_count);

    if (state->frame_count % 4 == 0) {
        capture_frame_image(state->renderer);
    }

    // SDL_RenderPresent blocks here until the next VSync interval
    SDL_RenderPresent(state->renderer);

    ++state->frame_count;
    FrameMark;
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, [[maybe_unused]] SDL_AppResult result)
{
    ZoneScopedN("app_quit");
    TracyMessageL("app_quit => shutting down");

    auto owner = std::unique_ptr<app_state>(static_cast<app_state*>(appstate));
    owner->running.store(false, std::memory_order_relaxed);

    for (auto& worker : owner->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    if (owner->renderer != nullptr) {
        SDL_DestroyRenderer(owner->renderer);
    }
    if (owner->window != nullptr) {
        SDL_DestroyWindow(owner->window);
    }
    SDL_Quit();
}

// NOLINTEND(readability-identifier-naming)
