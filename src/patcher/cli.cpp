#include "cli.hpp"

#include "patcher.hpp"

#include <cxxopts.hpp>

#include <cstdio>

namespace wemod::cli
{
namespace
{
constexpr std::string_view usage_tail{"(patch|restore) --install-dir <dir>"};

void print_line(bool is_error, const std::string& line)
{
    // One line, one write: keeps piped and GUI-captured ordering stable.
    const std::string text{line + "\n"};
    std::fwrite(text.data(), 1UZ, text.size(), is_error ? stderr : stdout);
}

// cxxopts builds the option table once; parse() reuses it for real argv and
// help_text() reuses it for --help so the two can never drift apart.
// Throws cxxopts::exceptions on bad input; parse() translates to .error.
cxxopts::Options& spec()
{
    static cxxopts::Options table{"wemod_enhancer", "Patch the WeMod client: Pro unlock, no auto-updates, F12 DevTools."};
    static const bool once{[] {
        table.add_options()("command", "patch | restore", cxxopts::value<std::string>())(
            "install-dir", "WeMod install holding resources/app.asar", cxxopts::value<std::string>())(
            "h,help", "Print this help and exit")("version", "Print the version and exit");
        table.parse_positional({"command"});
        return true;
    }()};
    (void)once;
    return table;
}
} // namespace

std::string help_text(const std::string& program)
{
    cxxopts::Options table{program, "Patch the WeMod client: Pro unlock, no auto-updates, F12 DevTools."};
    table.add_options()("command", "patch | restore", cxxopts::value<std::string>())(
        "install-dir", "WeMod install holding resources/app.asar", cxxopts::value<std::string>())(
        "h,help", "Print this help and exit")("version", "Print the version and exit");
    table.parse_positional({"command"});
    return "USAGE: " + program + " " + std::string{usage_tail} + "\n\n" + table.help();
}

options parse(int argc, const char* const* argv) noexcept
{
    options cli;
    try {
        const cxxopts::ParseResult parsed{spec().parse(argc, argv)};
        if (parsed.count("help") != 0UZ) {
            cli.show_help = true;
            return cli;
        }
        if (parsed.count("version") != 0UZ) {
            cli.show_version = true;
            return cli;
        }
        if (parsed.count("command") == 0UZ || parsed.count("install-dir") == 0UZ) {
            cli.error = "expected 'patch' or 'restore' with '--install-dir <dir>'";
            return cli;
        }
        cli.subcommand = parsed["command"].as<std::string>();
        cli.install_dir = parsed["install-dir"].as<std::string>();
        if (cli.subcommand != "patch" && cli.subcommand != "restore") {
            cli.error = "unknown command '" + cli.subcommand + "': expected 'patch' or 'restore'";
            cli.subcommand.clear();
            return cli;
        }
        return cli;
    } catch (const std::exception& failure) {
        cli.error = failure.what();
        return cli;
    }
}

int run(const std::string& program, const options& cli)
{
    if (cli.show_help) {
        print_line(false, help_text(program));
        return 0;
    }
    if (cli.show_version) {
        print_line(false, "wemod_enhancer " + std::string{WEMOD_ENHANCER_VERSION});
        return 0;
    }
    if (!cli.error.empty()) {
        print_line(true, "error: " + cli.error);
        print_line(true, help_text(program));
        return 2;
    }
    // Stream each line as it happens so long runs show progress.
    const log_sink stream = [](const std::string& line) {
        print_line(false, line);
        std::fflush(stdout);
    };
    const fs::path install{cli.install_dir};
    const patch_result result =
        cli.subcommand == "patch" ? patch_install(install, stream) : restore_install(install, stream);
    if (result) {
        return 0;
    }
    print_line(true, "error: " + result.error().message);
    return 1;
}
} // namespace wemod::cli
