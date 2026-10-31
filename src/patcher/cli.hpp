#pragma once

// CLI front end: LLVM-style `patch` / `restore` subcommands over the
// wemod::patcher library. Parses argv, prints help/version, streams the
// library log lines to stdout and errors to stderr. Returns process exit code.

#include <string>

namespace wemod::cli
{
/// Parsed command line. Empty subcommand means "not a CLI invocation".
struct options final
{
    std::string subcommand;
    std::string install_dir;
    bool show_help{false};
    bool show_version{false};
    std::string error;
};

/// Parse argc/argv (argv[0] is the program name). Never throws.
[[nodiscard]] options parse(int argc, const char* const* argv) noexcept;

/// LLVM-style help text, shared by --help and usage errors.
[[nodiscard]] std::string help_text(const std::string& program);

/// Run one subcommand against install_dir, streaming log lines.
/// Returns 0 on success, 1 on patch failure, 2 on usage error.
int run(const std::string& program, const options& cli);
} // namespace wemod::cli
