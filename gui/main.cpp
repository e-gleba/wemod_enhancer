// gui/main.cpp
//
// Dear ImGui (SDL3 + SDL_Renderer) frontend for the tested Python
// patcher CLI (tools/wemod_enhancer.py). Runs `patch` / `restore` for
// users who are not comfortable with a terminal and shows the script's
// stdout/stderr live, plus the exit code, in a scrolling log.
//
// The SDL3 renderer backend needs no OpenGL: SDL3 picks the platform's
// own rendering API (Direct3D on Windows, OpenGL/Vulkan/software on
// Linux) and loads it dynamically at runtime.
//
// NOTE: imgui's default font covers ASCII only - keep every literal in
// this file plain ASCII (no em-dashes, arrows or ellipsis characters).

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "imgui_stdlib.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct run_result
{
    int exit_code;
    std::string output;
};

struct app_state
{
    SDL_Window* window = nullptr;
    std::string install_dir;
    std::string script_path;
    std::string python;
    std::string version_dll; // empty = the script auto-detects it
    std::string log;
    std::future<run_result> pending;
    bool running = false;
    bool scroll_to_bottom = false;
    bool has_run = false;
    int last_exit_code = 0;
    float copied_flash = 0.0F; // seconds left of "Copied!" feedback
};

// Report a fatal startup error and return the process exit code.
// SDL_ShowSimpleMessageBox may be called before SDL_Init, so this
// works for every early failure - and GUI users actually see it.
int fatal_error(const std::string& what)
{
    const std::string message = what + ": " + SDL_GetError();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             "WeMod Enhancer",
                             message.c_str(),
                             nullptr);
    return 1;
}

// Quote one argument for the platform shell behind popen().
std::string shell_quote(const std::string& arg)
{
#ifdef _WIN32
    return "\"" + arg + "\"";
#else
    std::string quoted = "'";
    for (const char c : arg) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    return quoted + "'";
#endif
}

// Run the command, reading its stdout+stderr line by line as they are
// produced (popen), so the log streams live instead of appearing only
// after the process exits. Blocking - call from a worker thread.
run_result run_command(const std::string& command)
{
#ifdef _WIN32
    const std::string line = command + " 2>&1";
    FILE* pipe = _popen(line.c_str(), "r");
#else
    const std::string line = command + " 2>&1";
    FILE* pipe = popen(line.c_str(), "r");
#endif

    run_result result{ .exit_code = -1, .output = {} };
    if (pipe == nullptr) {
        result.output = "error: could not start the command\n";
        return result;
    }

    std::string pending_line;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        pending_line += buffer;
        // Flush on newline so the log grows visibly while the patcher
        // runs instead of arriving as one blob at the end.
        if (pending_line.find('\n') != std::string::npos) {
            result.output += pending_line;
            pending_line.clear();
        }
    }
    result.output += pending_line; // trailing partial line

#ifdef _WIN32
    result.exit_code = _pclose(pipe); // already the process exit code
#else
    const int status = pclose(pipe);
    if (status != -1 && WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }
#endif
    return result;
}

// "app-10.2.3" -> {10, 2, 3}; non-numeric tokens become 0.
std::vector<int> version_parts(std::string name)
{
    constexpr std::string_view prefix = "app-";
    if (name.starts_with(prefix)) {
        name.erase(0, prefix.size());
    }
    std::vector<int> parts;
    std::size_t pos = 0;
    while (pos < name.size()) {
        const std::size_t dot = name.find('.', pos);
        const std::string token =
            name.substr(pos, dot == std::string::npos ? dot : dot - pos);
        int value = 0;
        std::from_chars(token.data(), token.data() + token.size(), value);
        parts.push_back(value);
        if (dot == std::string::npos) {
            break;
        }
        pos = dot + 1;
    }
    return parts;
}

// Newest app-* directory under root that contains resources/app.asar
// (the layout the patcher expects as --install-dir).
fs::path find_app_dir(const fs::path& root)
{
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return {};
    }
    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec) ||
            !entry.path().filename().string().starts_with("app-") ||
            !fs::is_regular_file(entry.path() / "resources" / "app.asar",
                                 ec)) {
            continue;
        }
        candidates.push_back(entry.path());
    }
    const auto newest =
        std::ranges::max_element(candidates, {}, [](const fs::path& path) {
            return version_parts(path.filename().string());
        });
    return newest == candidates.end() ? fs::path{} : *newest;
}

std::string default_install_dir()
{
#ifdef _WIN32
    // Mirror the readme PowerShell snippet: newest
    // %LOCALAPPDATA%\WeMod\app-* that has resources\app.asar.
    if (const char* local = std::getenv("LOCALAPPDATA")) {
        const fs::path wemod = fs::path(local) / "WeMod";
        if (const fs::path app = find_app_dir(wemod); !app.empty()) {
            return app.string();
        }
        return wemod.string();
    }
#else
    // wemod-launcher layout (see readme "Linux / Steam Deck").
    if (const char* home = std::getenv("HOME")) {
        return (fs::path(home) / "wemod-launcher" / "wemod_data" /
                "wemod_bin")
            .string();
    }
#endif
    return {};
}

std::string default_script_path()
{
    // Install layout: wemod_enhancer.py sits next to the executable.
    if (const char* base = SDL_GetBasePath()) {
        if (const fs::path beside = fs::path(base) / "wemod_enhancer.py";
            fs::is_regular_file(beside)) {
            return beside.string();
        }
    }
    // Build-tree layout: fall back to the in-tree CLI.
    if (const fs::path in_tree = fs::path(WEMOD_ENHANCER_SOURCE_DIR) /
            "tools" / "wemod_enhancer.py";
        fs::is_regular_file(in_tree)) {
        return in_tree.string();
    }
    return {};
}

// A folder counts as a WeMod install when resources/app.asar exists
// inside it (the app-* layout the patcher patches).
bool looks_like_wemod_install(const std::string& dir)
{
    std::error_code ec;
    return !dir.empty() &&
        fs::is_regular_file(fs::path(dir) / "resources" / "app.asar", ec);
}

// SDL dialog callback: may run on another thread; it only writes a
// std::string that the UI thread reads next frame - safe in practice
// because the dialog is modal and the field is not edited meanwhile.
void SDLCALL on_folder_chosen(void* userdata,
                              const char* const* filelist,
                              int filter)
{
    (void)filter;
    auto* target = static_cast<std::string*>(userdata);
    if (filelist != nullptr && *filelist != nullptr) {
        *target = *filelist;
    }
}

void start_run(app_state& state, const char* subcommand)
{
    if (state.running) {
        return;
    }

    std::string shown = "$ " + state.python + " " + state.script_path +
        " " + subcommand + " --install-dir " + state.install_dir;
    std::string command = shell_quote(state.python) + " " +
        shell_quote(state.script_path) + " " + subcommand +
        " --install-dir " + shell_quote(state.install_dir);
    if (!state.version_dll.empty() &&
        std::string_view(subcommand) == "patch") {
        shown += " --version-dll " + state.version_dll;
        command += " --version-dll " + shell_quote(state.version_dll);
    }
    state.log += shown + "\n";

    state.running = true;
    state.has_run = true;
    state.scroll_to_bottom = true;
    state.pending = std::async(std::launch::async, run_command, command);
}

void poll_run(app_state& state)
{
    if (!state.running ||
        state.pending.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
        return;
    }
    const run_result result = state.pending.get();
    state.log += result.output;
    if (!result.output.empty() && !result.output.ends_with('\n')) {
        state.log += '\n';
    }
    state.log += "[exit code: " + std::to_string(result.exit_code) +
        "]\n\n";
    state.last_exit_code = result.exit_code;
    state.running = false;
    state.scroll_to_bottom = true;
}

void draw_ui(app_state& state)
{
    poll_run(state);

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##wemod_enhancer",
                 nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    // Steam-style layout: one compact control block pinned to the top
    // (title, the single required input, actions), then the log fills
    // the rest. Fixed-height sections keep every control reachable -
    // nothing is pushed below the window edge.

    ImGui::TextUnformatted("WeMod Enhancer");
    ImGui::SameLine();
    ImGui::TextDisabled("| close WeMod, pick its install folder, press Patch");
    ImGui::Spacing();

    // --- The only thing a user must provide: the WeMod folder -------
    const bool install_ok = looks_like_wemod_install(state.install_dir);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("WeMod folder");
    ImGui::SameLine();
    const float browse_w = ImGui::CalcTextSize("Browse...").x +
        ImGui::GetStyle().FramePadding.x * 2.0F;
    ImGui::SetNextItemWidth(-browse_w - ImGui::GetStyle().ItemSpacing.x);
    if (!install_ok && !state.install_dir.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Border,
                              ImVec4(0.90F, 0.30F, 0.30F, 1.00F));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
    }
    ImGui::InputTextWithHint(
        "##install_dir",
#ifdef _WIN32
        "C:\\Users\\<you>\\AppData\\Local\\WeMod\\app-10.x.x",
#else
        "~/wemod-launcher/wemod_data/wemod_bin",
#endif
        &state.install_dir);
    if (!install_ok && !state.install_dir.empty()) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        SDL_ShowOpenFolderDialog(on_folder_chosen,
                                 &state.install_dir,
                                 state.window,
                                 state.install_dir.empty()
                                     ? nullptr
                                     : state.install_dir.c_str(),
                                 false);
    }
    if (install_ok) {
        ImGui::TextColored(ImVec4(0.35F, 0.85F, 0.45F, 1.00F),
                           "Found resources/app.asar - looks good.");
    } else {
        ImGui::TextDisabled("The folder must contain resources\\app.asar");
    }

    ImGui::Spacing();

    // --- Actions ----------------------------------------------------
    const bool can_run = !state.running && install_ok;
    ImGui::BeginDisabled(!can_run);
    if (ImGui::Button("Patch", ImVec2(120.0F, 0.0F))) {
        start_run(state, "patch");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s",
                          install_ok ? "Unlock Pro features"
                                     : "Pick a valid WeMod folder first");
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore", ImVec2(120.0F, 0.0F))) {
        start_run(state, "restore");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s",
                          install_ok ? "Undo every change (uses backups)"
                                     : "Pick a valid WeMod folder first");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (state.running) {
        ImGui::TextUnformatted("Running - keep WeMod closed...");
    } else if (state.has_run && state.last_exit_code == 0) {
        ImGui::TextColored(ImVec4(0.35F, 0.85F, 0.45F, 1.00F),
                           "Done - everything went fine.");
    } else if (state.has_run) {
        const std::string status = "Failed (exit code " +
            std::to_string(state.last_exit_code) +
            ") - press \"Copy output\" and share it when asking for help.";
        ImGui::TextColored(ImVec4(0.90F, 0.30F, 0.30F, 1.00F),
                           "%s",
                           status.c_str());
    }

    ImGui::Separator();

    // --- Advanced: for power users / debugging ----------------------
    if (ImGui::CollapsingHeader("Advanced")) {
        ImGui::Indent();
        ImGui::TextUnformatted("Python interpreter");
        ImGui::SetNextItemWidth(200.0F);
        ImGui::InputTextWithHint("##python",
#ifdef _WIN32
                                 "python",
#else
                                 "python3",
#endif
                                 &state.python);
        ImGui::TextUnformatted("Patcher script (wemod_enhancer.py)");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##script", &state.script_path);
        ImGui::TextUnformatted("version.dll (empty = auto-detect)");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##version_dll",
                                 "auto: next to the script",
                                 &state.version_dll);
        ImGui::Unindent();
    }

    ImGui::Separator();

    // --- Output: bounded child, toolbar pinned below it --------------
    const float toolbar_height = ImGui::GetFrameHeightWithSpacing() +
        ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##log",
                      ImVec2(0.0F, -toolbar_height),
                      ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (state.log.empty()) {
        ImGui::TextDisabled("Output of the patcher will appear here.");
    } else {
        ImGui::TextUnformatted(state.log.c_str());
    }
    if (state.scroll_to_bottom) {
        ImGui::SetScrollHereY(1.0F);
        state.scroll_to_bottom = false;
    }
    ImGui::EndChild();

    if (ImGui::Button("Copy output")) {
        if (!state.log.empty()) {
            SDL_SetClipboardText(state.log.c_str());
            state.copied_flash = 1.5F;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "%s",
            "Copy everything above - paste it when asking for help");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        state.log.clear();
        state.has_run = false;
    }
    if (state.copied_flash > 0.0F) {
        state.copied_flash -= io.DeltaTime;
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35F, 0.85F, 0.45F, 1.00F), "Copied!");
    }

    ImGui::End();
}

} // namespace

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return fatal_error("SDL_Init()");
    }

    // Window + SDL_Renderer (stock imgui SDL3+SDL_Renderer example)
    const float main_scale =
        SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE |
        SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window =
        SDL_CreateWindow("WeMod Enhancer",
                         static_cast<int>(860 * main_scale),
                         static_cast<int>(620 * main_scale),
                         window_flags);
    if (window == nullptr) {
        return fatal_error("SDL_CreateWindow()");
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        return fatal_error("SDL_CreateRenderer()");
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window,
                          SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    app_state state;
    state.window = window;
    state.install_dir = default_install_dir();
    state.script_path = default_script_path();
#ifdef _WIN32
    state.python = "python";
#else
    state.python = "python3";
#endif

    const ImVec4 clear_color(0.10F, 0.10F, 0.12F, 1.00F);

    bool done = false;
    while (!done) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                done = true;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window)) {
                done = true;
            }
        }
        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0) {
            SDL_Delay(10);
            continue;
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        draw_ui(state);

        ImGui::Render();
        SDL_SetRenderScale(renderer,
                           io.DisplayFramebufferScale.x,
                           io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColorFloat(renderer,
                                    clear_color.x,
                                    clear_color.y,
                                    clear_color.z,
                                    clear_color.w);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),
                                              renderer);
        SDL_RenderPresent(renderer);
    }

    // Wait for a running patch/restore so the pipe reader in the worker
    // thread finishes before the process exits.
    if (state.pending.valid()) {
        state.pending.wait();
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
