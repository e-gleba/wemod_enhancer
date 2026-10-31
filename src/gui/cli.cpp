// CLI boundary: the only TU that includes <cxxopts.hpp>. The public
// surface (cli.hpp) is pure std C++23; shared identity comes from
// meta.hpp, output delivery stays on the SDL side.

#include "cli.hpp"

#include "meta.hpp"

#include <cxxopts.hpp>

#include <gsl/narrow>

#include <format>
#include <string>
#include <vector>

namespace wemod::gui::cli
{
// noexcept firewall by design: runs inside SDL's entry point, so any
// failure (allocation, format, parse) becomes exit_err or terminates
// instead of escaping into SDL. The format strings below are literals,
// so only OOM can escape — the firewall contract.
// NOLINTNEXTLINE(bugprone-exception-escape)
decision decide(std::span<char*> args) noexcept
try {
    if (args.empty()) {
        return {.mode = mode::gui};
    }
    // Direct-list-init: the constructor is explicit, so copy-list-init
    // with '=' cannot compile.
    cxxopts::Options options{std::string{program_name},
                             std::string{program_about}};
    options.add_options()("h,help", "Print help",
                          cxxopts::value<bool>()->default_value("false"))(
        "V,version", "Print version",
        cxxopts::value<bool>()->default_value("false"));
    // Consuming (int&, char**&) overload: recognised options are
    // removed from the copy; unknown ones throw cxxopts::OptionException.
    // Parsing a copy keeps SDL's argv untouched for the GUI path.
    std::vector<char*> owned{args.begin(), args.end()};
    int argc = gsl::narrow<int>(args.size());
    char** argv = owned.data();
    const cxxopts::ParseResult parsed = options.parse(argc, argv);
    // Both keys are registered above with defaults, so lookup cannot
    // miss; if it ever did, the throw lands in the firewall handlers.
    const auto is_set = [&](const char* name) {
        // NOLINTNEXTLINE
        return parsed[name].as<bool>();
    };
    if (is_set("help")) {
        return {.mode = mode::exit_ok, .output = options.help()};
    }
    if (is_set("version")) {
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
