#include "app.hpp"
#include "platform.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <gsl/assert>
#include <gsl/narrow>
#include <gsl/pointers>
#include <gsl/util>

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <string>
#include <system_error>

namespace wemod::gui
{
namespace
{
constexpr std::array<float, 4> clear_color{0.10F, 0.10F, 0.12F, 1.00F};
constexpr auto process_poll_interval{std::chrono::milliseconds{10}};

struct app final
{
    platform::context platform;
    app_state state;
    background_runner jobs;
    bool imgui_ready{false};
};

void start_command(app& value, const run_kind kind,
                   const std::string_view shown, std::vector<std::string> args)
{
    Expects(!shown.empty());
    Expects(!args.empty());
    if (value.jobs.busy() || !value.jobs.launch(kind, std::move(args))) {
        return;
    }
    append_log(value.state, std::format("$ {}\n", shown));
    value.state.kind = kind;
    value.state.running = true;
    value.state.has_run |= kind == run_kind::patcher;
}

void start_run(app& value, const std::string_view subcommand)
{
    Expects(subcommand == "patch" || subcommand == "restore");
    std::string shown{std::format("{} -u {} {} --install-dir {}",
                                  value.state.python, value.state.script_path,
                                  subcommand, value.state.install_dir)};
    std::vector<std::string> args{value.state.python, "-u",
                                  value.state.script_path,
                                  std::string{subcommand}, "--install-dir",
                                  value.state.install_dir};
    start_command(value, run_kind::patcher, shown, std::move(args));
}
