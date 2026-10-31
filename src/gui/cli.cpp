// CLI boundary: the only TU that includes <cxxopts.hpp>. The public
// surface (cli.hpp) is pure std C++23; shared identity comes from
// meta.hpp, output delivery stays on the SDL side.

#include "cli.hpp"

#include "meta.hpp"

#include <cxxopts.hpp>

#include <format>
#include <string>

namespace wemod::gui::cli
{
decision decide(int argc, char* argv[]) noexcept
try {
    if (argc <= 0 || argv == nullptr) {
        return {.mode = mode::gui};
    }
    cxxopts::Options options = {std::string{program_name},
                                std::string{program_about}};
    options.add_options()("h,help", "Print help",
                          cxxopts::value<bool>()->default_value("false"))(
        "V,version", "Print version",
        cxxopts::value<bool>()->default_value("false"));
    // Consuming (int&, char**&) overload: recognised options are
    // removed from argv; unknown ones throw cxxopts::OptionException.
    const cxxopts::ParseResult parsed = options.parse(argc, argv);
    if (parsed["help"].as<bool>()) {
        return {.mode = mode::exit_ok, .output = options.help()};
    }
    if (parsed["version"].as<bool>()) {
        return {.mode = mode::exit_ok,
                .output = std::format("{} {}\n", program_name,
                                      gui_version)};
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
