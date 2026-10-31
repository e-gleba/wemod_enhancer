// CLI boundary: the only TU that includes <cxxopts.hpp>. The public
// surface (cli.hpp) is pure std C++23; SDL and the GUI talk to it
// through decide()/emit() only.

#include "cli.hpp"

#include <cxxopts.hpp>

#include <cstdio>
#include <format>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#ifndef WEMOD_ENHANCER_GUI_VERSION
#define WEMOD_ENHANCER_GUI_VERSION "0.0.0"
#endif

namespace wemod::gui::cli
{
namespace
{
void attach_parent_console() noexcept
{
#ifdef _WIN32
    // WIN32 GUI apps own no console: borrow the parent terminal so
    // `gui.exe --help` prints where it was invoked. No parent (launched
    // from Explorer) means nowhere to print; the caller still exits.
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0) {
        return;
    }
    FILE* reopened{nullptr};
    (void)freopen_s(&reopened, "CONOUT$", "w", stdout);
    (void)freopen_s(&reopened, "CONOUT$", "w", stderr);
#else
    return;
#endif
}
} // namespace

decision decide(int argc, char* argv[]) noexcept
try {
    if (argc <= 0 || argv == nullptr) {
        return {.mode = mode::gui};
    }
    cxxopts::Options options{"wemod_enhancer_gui",
                             "WeMod Enhancer GUI and CLI"};
    options.add_options()("h,help", "Print help",
                          cxxopts::value<bool>()->default_value("false"))(
        "V,version", "Print version",
        cxxopts::value<bool>()->default_value("false"));
    // Consuming (int&, char**&) overload: recognised options are
    // removed from argv; unknown ones throw cxxopts::OptionException.
    const cxxopts::ParseResult parsed{options.parse(argc, argv)};
    if (parsed["help"].as<bool>()) {
        return {.mode = mode::exit_ok, .output = options.help()};
    }
    if (parsed["version"].as<bool>()) {
        return {.mode = mode::exit_ok,
                .output = std::format("wemod_enhancer_gui {}\n",
                                      WEMOD_ENHANCER_GUI_VERSION)};
    }
    return {.mode = mode::gui};
} catch (const std::exception& error) {
    return {.mode = mode::exit_err,
            .output =
                std::format("error: {}\nTry '--help'.\n", error.what())};
} catch (...) {
    return {.mode = mode::exit_err,
            .output = "error: failed to parse arguments\n"};
}

void emit(const decision& value) noexcept
{
    if (value.mode == mode::gui) {
        return;
    }
    attach_parent_console();
    FILE* stream{value.mode == mode::exit_err ? stderr : stdout};
    (void)std::fputs(value.output.c_str(), stream);
    (void)std::fflush(stream);
}
} // namespace wemod::gui::cli
