// CLI boundary: the only TU that includes <cxxopts.hpp>. The public
// surface (cli.hpp) is pure std C++23; output delivery stays on the
// SDL side, which already owns every platform quirk.

#include "cli.hpp"

#include <cxxopts.hpp>

#include <format>
#include <string>
#include <string_view>

#ifndef WEMOD_ENHANCER_GUI_VERSION
#define WEMOD_ENHANCER_GUI_VERSION "0.0.0"
#endif

namespace wemod::gui::cli
{
namespace
{
constexpr std::string_view program_name{"wemod_enhancer_gui"};
constexpr std::string_view program_about{"WeMod Enhancer GUI and CLI"};

static_assert(program_name == "wemod_enhancer_gui");
} // namespace

decision decide(int argc, char* argv[]) noexcept
try {
    if (argc <= 0 || argv == nullptr) {
        return {.mode = mode::gui};
    }
    cxxopts::Options options{std::string{program_name},
                             std::string{program_about}};
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
                .output = std::format("{} {}\n", program_name,
                                      WEMOD_ENHANCER_GUI_VERSION)};
    }
    return {.mode = mode::gui};
} catch (const std::exception& error) {
    return {.mode = mode::exit_err,
            .output = std::format("{}\nTry '--help'.\n", error.what())};
} catch (...) {
    return {.mode = mode::exit_err,
            .output = "failed to parse arguments\n"};
}
} // namespace wemod::gui::cli
