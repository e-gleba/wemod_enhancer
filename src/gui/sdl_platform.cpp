// WeMod Enhancer - SDL3 platform side (SDL3 + app.hpp only).
// No ImGui headers here. Services: window/renderer lifecycle,
// async folder dialog, URL / clipboard / alert outbox, SDL file
// locations, log persistence via SDL IOStream, fatal message box.

#include "app.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cfloat>
#include <format>
#include <memory>

namespace wemod::gui::platform
{

namespace
{

using sdl_str = std::unique_ptr<char, decltype(&SDL_free)>;

[[nodiscard]] sdl_str adopt(char* p) noexcept { return {p, &SDL_free}; }

void box(const context& ctx, const Uint32 flags, const std::string_view title,
         const std::string_view message) noexcept
{
    const std::string t{title};
    const std::string m{message};
    SDL_ShowSimpleMessageBox(flags, t.c_str(), m.c_str(),
                             static_cast<SDL_Window*>(ctx.window));
}

void SDLCALL on_folder(void* userdata, const char* const* filelist,
                       int /*filter*/)
{
    auto* state = static_cast<app_state*>(userdata);
    if (state != nullptr && filelist != nullptr &&
        filelist[0] != nullptr) {
        state->install_dir = filelist[0];
    }
}

[[nodiscard]] std::string pref_file(const std::string_view name)
{
    sdl_str pref{adopt(SDL_GetPrefPath("wemod", "enhancer"))};
    if (pref == nullptr) {
        return {};
    }
    return (fs::path(pref.get()) / name).string();
}

} // namespace

std::pair<std::int32_t, std::int32_t> preferred_size() noexcept
{
    SDL_Rect usable{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) ||
        usable.w <= 0 || usable.h <= 0) {
        return {kWinFallbackW, kWinFallbackH};
    }
    return {std::clamp(usable.w / 2, kWinMinW, kWinMaxW),
            std::clamp(usable.h * 3 / 5, kWinMinH, kWinMaxH)};
}

std::string exe_dir()
{
    sdl_str base{adopt(SDL_GetBasePath())};
    if (base != nullptr) {
        return {base.get()};
    }
    std::error_code ec;
    return (fs::temp_directory_path(ec) / "wemod_enhancer").string();
}

std::string default_install_dir()
{
    SDL_Environment* env{SDL_GetEnvironment()};
    if constexpr (kIsWindows) {
        if (const char* local{SDL_GetEnvironmentVariable(env, "LOCALAPPDATA")}) {
            return (fs::path(local) / "WeMod").string();
        }
    } else {
        if (const char* home{SDL_GetEnvironmentVariable(env, "HOME")}) {
            return (fs::path(home) / "wemod-launcher").string();
        }
    }
    return {};
}

std::string platform_name() { return {SDL_GetPlatform()}; }

bool init(context& ctx, app_state& state)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fatal("SDL_Init failed", SDL_GetError());
        return false;
    }
    const auto [win_w, win_h]{preferred_size()};
    SDL_Window* window{nullptr};
    SDL_Renderer* renderer{nullptr};
    if (!SDL_CreateWindowAndRenderer("WeMod Enhancer", win_w, win_h,
                                     SDL_WINDOW_RESIZABLE |
                                         SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &window, &renderer)) {
        fatal("Cannot create window", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window, kWinMinW, kWinMinH);
    SDL_SetRenderVSync(renderer, 1);
    ctx.window = window;
    ctx.renderer = renderer;
    state.platform_name = platform_name();
    state.exe_dir_text = exe_dir();
    return true;
}

void drain_outbox(context& ctx, app_state& state, const float delta_seconds)
{
    if (state.copied_flash > 0.0F) {
        state.copied_flash =
            std::max(0.0F, state.copied_flash - delta_seconds);
    }
    if (state.want_browse) {
        state.want_browse = false;
        SDL_ShowOpenFolderDialog(on_folder, &state,
                                static_cast<SDL_Window*>(ctx.window),
                                state.install_dir.empty()
                                    ? nullptr
                                    : state.install_dir.c_str(),
                                false);
    }
    if (state.want_open_url.has_value()) {
        const std::string url{std::move(*state.want_open_url)};
        state.want_open_url.reset();
        if (!SDL_OpenURL(url.c_str())) {
            SDL_Log("SDL_OpenURL: %s", SDL_GetError());
        }
    }
    if (state.want_clipboard.has_value()) {
        const std::string text{std::move(*state.want_clipboard)};
        state.want_clipboard.reset();
        if (!SDL_SetClipboardText(text.c_str())) {
            SDL_Log("SDL_SetClipboardText: %s", SDL_GetError());
        } else {
            state.copied_flash = 1.5F;
        }
    }
    if (state.want_alert.has_value()) {
        alert_request alert{std::move(*state.want_alert)};
        state.want_alert.reset();
        box(ctx, SDL_MESSAGEBOX_WARNING, alert.title, alert.message);
    }
}

void begin_frame(context& ctx, const float scale_x, const float scale_y,
                 const rgba& clear)
{
    auto* renderer = static_cast<SDL_Renderer*>(ctx.renderer);
    SDL_SetRenderScale(renderer, scale_x, scale_y);
    SDL_SetRenderDrawColorFloat(renderer, clear.r, clear.g, clear.b, clear.a);
    SDL_RenderClear(renderer);
}

void end_frame(context& ctx)
{
    SDL_RenderPresent(static_cast<SDL_Renderer*>(ctx.renderer));
}

void shutdown(context& ctx)
{
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
    const std::string t{title};
    const std::string m{message};
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, t.c_str(), m.c_str(),
                             nullptr);
    SDL_Log("%s: %s", t.c_str(), m.c_str());
}

void persist_log(const app_state& state)
{
    const std::string path{pref_file("gui.log")};
    if (path.empty() || state.log.empty()) {
        return;
    }
    const std::string tail{state.log.size() > kLogBudget
                               ? state.log.substr(state.log.size() -
                                                  kLogBudget)
                               : state.log};
    const std::string body{tail + "\n" + env_info(state)};
    SDL_IOStream* io{SDL_IOFromFile(path.c_str(), "w")};
    if (io == nullptr) {
        return;
    }
    SDL_WriteIO(io, body.data(), body.size());
    SDL_CloseIO(io);
}

} // namespace wemod::gui::platform
