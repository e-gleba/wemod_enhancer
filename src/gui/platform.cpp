// SDL3 platform layer implementation. One rule: every SDL call is checked;
// every failure logs (SDL_Log) and the fatal ones also raise the native
// assert window (SDL_ShowSimpleMessageBox). No errno, no popen, no shell.
#include "platform.hpp"

#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <memory>
#include <mutex>
#include <system_error>

namespace wemod::gui
{

namespace
{

// Latest SDL error text, never null.
[[nodiscard]] const char* sdl_err() noexcept
{
    const char* err{SDL_GetError()};
    return (err != nullptr && err[0] != '\0') ? err : "<no sdl error>";
}

// Message-box capable this early/late in startup: SDL_ShowSimpleMessageBox
// works before SDL_Init, but needs video once we depend on a parent.
void show_box(SDL_MessageBoxFlags flags, const char* title,
              const std::string& message, SDL_Window* parent) noexcept
{
    if (!SDL_ShowSimpleMessageBox(flags, title, message.c_str(), parent)) {
        SDL_Log("messagebox failed (%s): %s", sdl_err(), message.c_str());
    }
}

} // namespace

void fatal_message(const char* title, const std::string& message) noexcept
{
    SDL_Log("fatal: %s", message.c_str());
    show_box(SDL_MESSAGEBOX_ERROR, title, message, nullptr);
}

void log_error(const std::string& message) noexcept
{
    SDL_Log("%s", message.c_str());
}

run_result run_process(const std::vector<std::string>& args)
{
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1UZ);
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);
    return run_process_argv(argv);
}

run_result run_process_argv(const std::vector<const char*>& args)
{
    run_result result;
    if (args.empty() || args.front() == nullptr) {
        result.output = "error: empty argv - nothing to spawn\n";
        return result;
    }

    const SDL_PropertiesID props{SDL_CreateProperties()};
    if (props == 0) {
        result.output =
            std::string{"error: SDL_CreateProperties: "} + sdl_err() + "\n";
        return result;
    }
    // Argv form: no shell, no quoting. Merged stderr->stdout so one
    // SDL_ReadProcess captures both streams with no pipe-drain thread.
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                           const_cast<const char**>(args.data()));
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
                          SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(
        props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);

    SDL_Process* proc{SDL_CreateProcessWithProperties(props)};
    SDL_DestroyProperties(props);
    if (proc == nullptr) {
        result.output =
            std::string{"error: SDL_CreateProcess: "} + sdl_err() + "\n";
        return result;
    }

    // Blocking read: the worker is a background jthread, the UI never waits.
    std::size_t size{0};
    int code{-1};
    void* data{SDL_ReadProcess(proc, &size, &code)};
    if (data == nullptr) {
        result.output =
            std::string{"error: SDL_ReadProcess: "} + sdl_err() + "\n";
        SDL_WaitProcess(proc, true, nullptr);
        SDL_DestroyProcess(proc);
        return result;
    }
    result.output.assign(static_cast<const char*>(data), size);
    SDL_free(data);

    int exit_code{-1};
    if (!SDL_WaitProcess(proc, true, &exit_code)) {
        log_error(std::string{"SDL_WaitProcess: "} + sdl_err());
    } else {
        code = exit_code;
    }
    SDL_DestroyProcess(proc);
    result.exit_code = static_cast<std::int32_t>(code);
    return result;
}

std::vector<std::int32_t> version_parts(std::string_view name)
{
    constexpr std::string_view prefix{"app-"};
    if (name.starts_with(prefix)) {
        name.remove_prefix(prefix.size());
    }
    std::vector<std::int32_t> parts;
    std::size_t pos{0};
    while (pos < name.size()) {
        const std::size_t dot{name.find('.', pos)};
        const std::string_view token{
            name.data() + pos,
            (dot == std::string_view::npos ? name.size() : dot) - pos};
        std::int32_t value{0};
        if (const auto res{
                std::from_chars(token.data(), token.data() + token.size(), value)};
            res.ec != std::errc{} || res.ptr != token.data() + token.size()) {
            value = 0;
        }
        parts.push_back(value);
        if (dot == std::string_view::npos) {
            break;
        }
        pos = dot + 1;
    }
    return parts;
}

namespace
{

struct app_scan final
{
    std::string best;
    std::vector<std::int32_t> best_parts;
    bool have{false};
};

SDL_EnumerationResult SDLCALL on_dir_entry(void* userdata, const char* dirname,
                                           const char* fname)
{
    auto* scan{static_cast<app_scan*>(userdata)};
    if (fname == nullptr || dirname == nullptr) {
        return SDL_ENUM_CONTINUE;
    }
    if (std::string_view{fname}.starts_with("app-")) {
        // fs::path join: SDL does not promise a trailing separator.
        const std::string full{(fs::path(dirname) / fname).string()};
        SDL_PathInfo info{};
        if (SDL_GetPathInfo(full.c_str(), &info) &&
            info.type == SDL_PATHTYPE_DIRECTORY) {
            const std::vector<std::int32_t> parts{version_parts(fname)};
            // Lexicographic vector compare: first differing token wins.
            const bool newer{!scan->have || scan->best_parts < parts ||
                             (scan->best_parts == parts && scan->best < full)};
            if (newer) {
                scan->best = full;
                scan->best_parts = parts;
                scan->have = true;
            }
        }
    }
    return SDL_ENUM_CONTINUE;
}

} // namespace

fs::path newest_app_dir(const fs::path& root)
{
    try {
        app_scan scan;
        const std::string dir{root.string()};
        if (dir.empty()) {
            return {};
        }
        if (!SDL_EnumerateDirectory(dir.c_str(), on_dir_entry, &scan)) {
            log_error(std::string{"SDL_EnumerateDirectory: "} + sdl_err());
            return {};
        }
        if (!scan.have) {
            return {};
        }
        return {scan.best};
    } catch (...) {
        log_error("newest_app_dir: path conversion failed");
        return {};
    }
}

std::string default_install_dir()
{
    if constexpr (is_windows) {
        if (const char* local{SDL_GetEnvironmentVariable(
                SDL_GetEnvironment(), "LOCALAPPDATA")}) {
            return (fs::path(local) / "WeMod").string();
        }
    } else {
        if (const char* home{SDL_GetEnvironmentVariable(SDL_GetEnvironment(),
                                                        "HOME")}) {
            return (fs::path(home) / "wemod-launcher").string();
        }
    }
    return {};
}

fs::path exe_dir()
{
    if (const char* base{SDL_GetBasePath()}) {
        return {base};
    }
    log_error(std::string{"SDL_GetBasePath: "} + sdl_err());
    std::error_code ec;
    return fs::temp_directory_path(ec) / "wemod_enhancer";
}

std::string downloads_dir()
{
    // SDL_GetUserFolder returns SDL-owned memory - do not free.
    if (const char* downloads{SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)}) {
        return {downloads};
    }
    log_error(std::string{"SDL_GetUserFolder: "} + sdl_err());
    return {};
}

std::string home_dir()
{
    if (const char* home{SDL_GetEnvironmentVariable(SDL_GetEnvironment(),
                                                    "HOME")}) {
        return {home};
    }
    return {};
}

std::string url_encode(std::string_view text)
{
    constexpr std::string_view unreserved{
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~"};
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text) {
        if (unreserved.find(static_cast<char>(c)) != std::string_view::npos) {
            out += static_cast<char>(c);
        } else {
            constexpr char hex[]{"0123456789ABCDEF"};
            out += '%';
            out += hex[(c >> 4) & 0xFU];
            out += hex[c & 0xFU];
        }
    }
    return out;
}

std::string pick_folder(void* window_handle, const std::string& current)
{
    SDL_Window* window{static_cast<SDL_Window*>(window_handle)};
    // Async callback -> sync result. The SDL dialog callback may fire on
    // another thread and (on cancel/error paths) may fire after this
    // function returns: shared ownership keeps the state alive until
    // the callback runs, instead of a stack struct that dangles.
    struct choice final
    {
        std::mutex mutex;
        std::string path;
        bool done{false};
    };
    auto result{std::make_shared<choice>()};
    // Copies the shared_ptr so the callback holds one ref too.
    const auto callback = [](void* userdata, const char* const* filelist,
                             int) {
        // Takes over the heap box holding the shared_ptr copy.
        const std::unique_ptr<std::shared_ptr<choice>> owned{
            static_cast<std::shared_ptr<choice>*>(userdata)};
        auto& out{**owned};
        std::lock_guard lock{out.mutex};
        if (filelist != nullptr && filelist[0] != nullptr) {
            out.path = filelist[0];
        }
        out.done = true;
    };
    // Heap box: SDL takes a raw void*; the callback deletes exactly it.
    auto* boxed{new std::shared_ptr<choice>(result)};
    SDL_ShowOpenFolderDialog(callback, boxed, window,
                             current.empty() ? nullptr : current.c_str(), false);
    // Modal dialog pumps its own loop; poll until the callback fires.
    // Timeout only abandons the wait - the shared state still outlives
    // a late callback instead of dangling.
    std::string picked;
    for (int spins{0}; spins < 36000; ++spins) {
        {
            std::lock_guard lock{result->mutex};
            if (result->done) {
                picked = result->path;
                break;
            }
        }
        SDL_Delay(5);
        SDL_PumpEvents();
    }
    if (picked.empty()) {
        std::lock_guard lock{result->mutex};
        if (!result->done) {
            // Late callback still owns its boxed ref and will free it.
            log_error("SDL_ShowOpenFolderDialog: timed out waiting for "
                      "selection");
            return {};
        }
        picked = result->path;
    }
    return picked;
}

bool set_clipboard_text(const std::string& text)
{
    if (!SDL_SetClipboardText(text.c_str())) {
        log_error(std::string{"SDL_SetClipboardText: "} + sdl_err());
        return false;
    }
    return true;
}

bool open_url(const char* url) noexcept
{
    if (url == nullptr || url[0] == '\0') {
        return false;
    }
    if (!SDL_OpenURL(url)) {
        log_error(std::string{"SDL_OpenURL: "} + sdl_err());
        return false;
    }
    return true;
}

std::string platform_name()
{
    const char* name{SDL_GetPlatform()};
    return (name != nullptr) ? name : "unknown";
}

std::pair<std::int32_t, std::int32_t> pick_window_size()
{
    SDL_Rect usable{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) ||
        usable.w <= 0 || usable.h <= 0) {
        log_error(std::string{"SDL_GetDisplayUsableBounds: "} + sdl_err());
        return {window_width_fallback, window_height_fallback};
    }
    const std::int32_t width{std::clamp(usable.w / 2, window_min_width,
                                        window_max_width)};
    const std::int32_t height{std::clamp((usable.h * 3) / 5, window_min_height,
                                         window_max_height)};
    return {width, height};
}

window make_window()
{
    const auto [win_w, win_h]{pick_window_size()};
    SDL_Window* win{nullptr};
    SDL_Renderer* ren{nullptr};
    if (!SDL_CreateWindowAndRenderer("WeMod Enhancer", win_w, win_h,
                                     SDL_WINDOW_RESIZABLE |
                                         SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &win, &ren)) {
        fatal_message("WeMod Enhancer",
                      std::string{"SDL_CreateWindowAndRenderer: "} + sdl_err());
        return {};
    }
    SDL_SetWindowMinimumSize(win, window_min_width, window_min_height);
    if (!SDL_SetRenderVSync(ren, 1)) {
        // Non-fatal: vsync off still runs, just tears.
        log_error(std::string{"SDL_SetRenderVSync: "} + sdl_err());
    }
    window out;
    out.handle = win;
    out.renderer = ren;
    return out;
}

bool begin_frame(SDL_Renderer* renderer, float scale_x, float scale_y, float r,
                 float g, float b, float a)
{
    if (renderer == nullptr) {
        return false;
    }
    // The one HiDPI knob: map imgui window coords onto the framebuffer.
    if (!SDL_SetRenderScale(renderer, scale_x, scale_y)) {
        log_error(std::string{"SDL_SetRenderScale: "} + sdl_err());
        return false;
    }
    if (!SDL_SetRenderDrawColorFloat(renderer, r, g, b, a)) {
        log_error(std::string{"SDL_SetRenderDrawColorFloat: "} + sdl_err());
        return false;
    }
    if (!SDL_RenderClear(renderer)) {
        log_error(std::string{"SDL_RenderClear: "} + sdl_err());
        return false;
    }
    return true;
}

bool present_frame(SDL_Renderer* renderer)
{
    if (!SDL_RenderPresent(renderer)) {
        log_error(std::string{"SDL_RenderPresent: "} + sdl_err());
        return false;
    }
    return true;
}

} // namespace wemod::gui
