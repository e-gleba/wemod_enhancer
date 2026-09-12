#include "app.hpp"
#include "platform.hpp"

#include <SDL3/SDL.h>

#include <gsl/assert>
#include <gsl/narrow>

#include <algorithm>
#include <format>
#include <system_error>

namespace wemod::gui::platform
{
namespace
{
constexpr std::int32_t window_fallback_width{1024};
constexpr std::int32_t window_fallback_height{680};
constexpr std::int32_t window_min_width{880};
constexpr std::int32_t window_min_height{600};
constexpr std::int32_t window_max_width{1680};
constexpr std::int32_t window_max_height{1050};

struct sdl_string final
{
    char* value{nullptr};

    explicit sdl_string(char* owned) noexcept : value{owned} {}
    sdl_string(const sdl_string&) = delete;
    sdl_string& operator=(const sdl_string&) = delete;
    sdl_string(sdl_string&&) = delete;
    sdl_string& operator=(sdl_string&&) = delete;
    ~sdl_string() noexcept { SDL_free(value); }
};

void log_message(const std::string_view message) noexcept
{
    const std::string text{message};
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_ERROR, "%s",
                   text.c_str()); // NOLINT(cppcoreguidelines-pro-type-vararg)
}

void message_box(SDL_Window* window, const SDL_MessageBoxFlags flags,
                 const std::string_view title,
                 const std::string_view message) noexcept
{
    const std::string title_text{title};
    const std::string message_text{message};
    if (!SDL_ShowSimpleMessageBox(flags, title_text.c_str(),
                                  message_text.c_str(), window)) {
        log_message(std::format("SDL_ShowSimpleMessageBox: {}",
                                SDL_GetError()));
    }
}

void SDLCALL on_folder(void* userdata, const char* const* file_list,
                       int filter) noexcept
{
    (void)filter;
    auto* result{static_cast<dialog_result*>(userdata)};
    if (result == nullptr) {
        return;
    }
    const std::lock_guard lock{result->mutex};
    if (file_list != nullptr && file_list[0] != nullptr) {
        result->folder = file_list[0];
    }
    result->pending = false;
}

[[nodiscard]] std::string preference_file(const std::string_view name)
{
    sdl_string preference{SDL_GetPrefPath("wemod", "enhancer")};
    if (preference.value == nullptr) {
        return {};
    }
    return (fs::path{preference.value} / name).string();
}

void log_error(const std::string_view operation) noexcept
{
    log_message(std::format("{}: {}", operation, SDL_GetError()));
}
}

native_context native(const context& ctx) noexcept
{
    return {static_cast<SDL_Window*>(ctx.window),
            static_cast<SDL_Renderer*>(ctx.renderer)};
}

std::pair<std::int32_t, std::int32_t> preferred_size() noexcept
{
    SDL_Rect usable{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) ||
        usable.w <= 0 || usable.h <= 0) {
        return {window_fallback_width, window_fallback_height};
    }
    return {std::clamp(gsl::narrow<std::int32_t>(usable.w) / 2,
                       window_min_width, window_max_width),
            std::clamp(gsl::narrow<std::int32_t>(usable.h) * 3 / 5,
                       window_min_height, window_max_height)};
}

std::string exe_dir()
{
    if (const char* base{SDL_GetBasePath()}) {
        return base;
    }
    std::error_code error;
    return (fs::temp_directory_path(error) / "wemod_enhancer").string();
}

std::string default_install_dir()
{
    SDL_Environment* environment{SDL_GetEnvironment()};
    if constexpr (is_windows) {
        if (const char* local{
                SDL_GetEnvironmentVariable(environment, "LOCALAPPDATA")}) {
            return (fs::path{local} / "WeMod").string();
        }
    } else if (const char* home{
                   SDL_GetEnvironmentVariable(environment, "HOME")}) {
        return (fs::path{home} / "wemod-launcher").string();
    }
    return {};
}

std::string platform_name()
{
    return SDL_GetPlatform();
}

bool init(context& ctx, app_state& state) noexcept
try {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fatal("SDL_Init failed", SDL_GetError());
        return false;
    }

    const auto [width, height]{preferred_size()};
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    if (!SDL_CreateWindowAndRenderer(
            "WeMod Enhancer", width, height,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &window,
            &renderer)) {
        fatal("Cannot create window", SDL_GetError());
        SDL_Quit();
        return false;
    }

    ctx.window = window;
    ctx.renderer = renderer;
    ctx.dialog = std::make_unique<dialog_result>();

    if (!SDL_SetWindowMinimumSize(window, window_min_width,
                                  window_min_height)) {
        log_error("SDL_SetWindowMinimumSize");
    }
    if (!SDL_SetRenderVSync(renderer, 1)) {
        log_error("SDL_SetRenderVSync");
    }

    state.platform_name = platform_name();
    state.exe_dir_text = exe_dir();
    return true;
} catch (const std::exception& error) {
    fatal("SDL initialization failed", error.what());
    shutdown(ctx);
    return false;
} catch (...) {
    fatal("SDL initialization failed", "Unknown error");
    shutdown(ctx);
    return false;
}

void drain_outbox(context& ctx, app_state& state,
                  const float delta_seconds) noexcept
try {
    const native_context handles{native(ctx)};
    Expects(handles.window != nullptr);
    Expects(handles.renderer != nullptr);
    Expects(ctx.dialog != nullptr);

    state.copied_flash =
        std::max(0.0F, state.copied_flash - delta_seconds);
    {
        const std::lock_guard lock{ctx.dialog->mutex};
        if (ctx.dialog->folder) {
            state.install_dir = std::move(*ctx.dialog->folder);
            ctx.dialog->folder.reset();
        }
    }

    if (std::exchange(state.want_browse, false)) {
        bool open{false};
        {
            const std::lock_guard lock{ctx.dialog->mutex};
            if (!ctx.dialog->pending) {
                ctx.dialog->pending = true;
                open = true;
            }
        }
        if (open) {
            SDL_ShowOpenFolderDialog(on_folder, ctx.dialog.get(),
                                     handles.window, nullptr, false);
        }
    }
    if (state.want_open_url) {
        const std::string url{std::move(*state.want_open_url)};
        state.want_open_url.reset();
        if (!SDL_OpenURL(url.c_str())) {
            log_error("SDL_OpenURL");
        }
    }
    if (state.want_clipboard) {
        const std::string text{std::move(*state.want_clipboard)};
        state.want_clipboard.reset();
        if (!SDL_SetClipboardText(text.c_str())) {
            log_error("SDL_SetClipboardText");
        } else {
            state.copied_flash = 1.5F;
        }
    }
    if (state.want_alert) {
        alert_request alert{std::move(*state.want_alert)};
        state.want_alert.reset();
        message_box(handles.window, SDL_MESSAGEBOX_WARNING, alert.title,
                    alert.message);
    }
} catch (const std::exception& error) {
    log_message(std::format("drain_outbox: {}", error.what()));
} catch (...) {
    log_message("drain_outbox: unknown error");
}

void begin_frame(context& ctx, const float scale_x, const float scale_y,
                 const std::array<float, 4>& clear) noexcept
{
    const native_context handles{native(ctx)};
    Expects(handles.renderer != nullptr);
    if (!SDL_SetRenderScale(handles.renderer, scale_x, scale_y)) {
        log_error("SDL_SetRenderScale");
    }
    if (!SDL_SetRenderDrawColorFloat(handles.renderer, clear[0], clear[1],
                                     clear[2], clear[3])) {
        log_error("SDL_SetRenderDrawColorFloat");
    }
    if (!SDL_RenderClear(handles.renderer)) {
        log_error("SDL_RenderClear");
    }
}

void end_frame(context& ctx) noexcept
{
    const native_context handles{native(ctx)};
    Expects(handles.renderer != nullptr);
    if (!SDL_RenderPresent(handles.renderer)) {
        log_error("SDL_RenderPresent");
    }
}

void shutdown(context& ctx) noexcept
{
    ctx.dialog.reset();
    if (ctx.renderer != nullptr) {
        SDL_DestroyRenderer(static_cast<SDL_Renderer*>(ctx.renderer));
        ctx.renderer = nullptr;
    }
    if (ctx.window != nullptr) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(ctx.window));
        ctx.window = nullptr;
    }
    SDL_Quit();
}

void fatal(const std::string_view title,
           const std::string_view message) noexcept
{
    message_box(nullptr, SDL_MESSAGEBOX_ERROR, title, message);
    log_message(std::format("{}: {}", title, message));
}

void persist_log(const app_state& state) noexcept
try {
    const std::string path{preference_file("gui.log")};
    if (path.empty() || state.log.empty()) {
        return;
    }
    const std::string body{state.log + "\n" + env_info(state)};
    SDL_IOStream* stream{SDL_IOFromFile(path.c_str(), "w")};
    if (stream == nullptr) {
        log_error("SDL_IOFromFile");
        return;
    }
    if (SDL_WriteIO(stream, body.data(), body.size()) != body.size()) {
        log_error("SDL_WriteIO");
    }
    if (!SDL_CloseIO(stream)) {
        log_error("SDL_CloseIO");
    }
} catch (const std::exception& error) {
    log_message(std::format("persist_log: {}", error.what()));
} catch (...) {
    log_message("persist_log: unknown error");
}
}
