// WeMod Enhancer - SDL3 platform side (SDL3 + app.hpp only).
// No ImGui headers here. Services: window/renderer lifecycle,
// async folder dialog, URL / clipboard / alert outbox, SDL file
// locations, log persistence via SDL IOStream, fatal message box.
// The header stays std-only: the SDL_Window* never leaks out, the
// view talks to this side through app_state data only.

#include "app.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <system_error>

namespace wemod::gui::platform
{

namespace
{

constexpr std::int32_t kWinFallbackW{1024};
constexpr std::int32_t kWinFallbackH{680};
constexpr std::int32_t kWinMinW{880};
constexpr std::int32_t kWinMinH{600};
constexpr std::int32_t kWinMaxW{1680};
constexpr std::int32_t kWinMaxH{1050};

using sdl_str = std::unique_ptr<char, decltype(&SDL_free)>;

[[nodiscard]] sdl_str adopt(char* owned) noexcept
{
    return {owned, &SDL_free};
}

void box(SDL_Window* window, const Uint32 flags,
         const std::string_view title,
         const std::string_view message) noexcept
{
    // SDL copies the strings synchronously.
    const std::string title_text{title};
    const std::string message_text{message};
    SDL_ShowSimpleMessageBox(flags, title_text.c_str(),
                             message_text.c_str(), window);
}

// SDL may invoke this on its own thread: touch only the mutex-guarded
// picked_folder handoff, never the live field text.
void SDLCALL on_folder(void* userdata, const char* const* filelist,
                       int /*filter*/)
{
    auto* state = static_cast<app_state*>(userdata);
    if (state == nullptr) {
        return;
    }
    const std::lock_guard lock{state->outbox_mutex};
    if (filelist != nullptr && filelist[0] != nullptr) {
        state->picked_folder = filelist[0];
    }
}

[[nodiscard]] std::string pref_file(const std::string_view name)
{
    sdl_str pref{adopt(SDL_GetPrefPath("wemod", "enhancer"))};
    if (pref == nullptr) {
        return {};
    }
    std::error_code ec;
    const fs::path file{fs::path(pref.get()) / name};
    std::error_code dir_ec;
    fs::create_directories(file.parent_path(), dir_ec);
    (void)ec;
    return file.string();
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
    // SDL-owned internal memory (cached): must NOT be freed.
    if (const char* base{SDL_GetBasePath()}) {
        return {base};
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
        SDL_Quit();
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
    auto* window = static_cast<SDL_Window*>(ctx.window);
    if (state.copied_flash > 0.0F) {
        state.copied_flash =
            std::max(0.0F, state.copied_flash - delta_seconds);
    }
    // Dialog result first: the field keeps user edits, the dialog only
    // delivers the picked path through the mutex handoff.
    {
        const std::lock_guard lock{state.outbox_mutex};
        if (state.picked_folder.has_value()) {
            state.install_dir = std::move(*state.picked_folder);
            state.picked_folder.reset();
        }
    }
    if (state.want_browse) {
        state.want_browse = false;
        // Keep the default path alive while the async dialog is open:
        // the field may be edited meanwhile, SDL only reads the pointer.
        ctx.dialog_default = state.install_dir;
        SDL_ShowOpenFolderDialog(on_folder, &state, window,
                                ctx.dialog_default.empty()
                                    ? nullptr
                                    : ctx.dialog_default.c_str(),
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
        box(window, SDL_MESSAGEBOX_WARNING, alert.title, alert.message);
    }
}

void begin_frame(context& ctx, const float scale_x, const float scale_y,
                 const std::array<float, 4>& clear)
{
    // Single DPI knob: map imgui window coords onto the
    // HIGH_PIXEL_DENSITY framebuffer. Layout stays 1x.
    auto* renderer = static_cast<SDL_Renderer*>(ctx.renderer);
    SDL_SetRenderScale(renderer, scale_x, scale_y);
    SDL_SetRenderDrawColorFloat(renderer, clear[0], clear[1], clear[2],
                               clear[3]);
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
    box(nullptr, SDL_MESSAGEBOX_ERROR, title, message);
    const std::string title_text{title};
    const std::string message_text{message};
    SDL_Log("%s: %s", title_text.c_str(), message_text.c_str());
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
    if (SDL_IOStream* io{SDL_IOFromFile(path.c_str(), "w")};
        io != nullptr) {
        SDL_WriteIO(io, body.data(), body.size());
        if (!SDL_CloseIO(io)) {
            SDL_Log("SDL_CloseIO: %s", SDL_GetError());
        }
    }
}

} // namespace wemod::gui::platform
