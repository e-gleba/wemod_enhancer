#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace wemod
{
namespace fs = std::filesystem;

/// Outcome of one patch or restore run.
struct patch_report final
{
    fs::path install_dir;
    std::vector<std::string> log_lines;
    bool fuse_removed{false};
};

/// Failure of one patch or restore run.
struct patch_error final
{
    std::string message;
};

/// Fallible result: value or failure message.
using patch_result = std::expected<patch_report, patch_error>;

/// Log sink: receives one line per event, without trailing newline.
using log_sink = std::function<void(const std::string&)>;

/// Patch the WeMod install at the given directory.
/// Reads resources/app.asar, applies bundle patches, repacks, flips fuse.
[[nodiscard]] patch_result patch_install(const fs::path& install_dir);

/// Restore the WeMod install at the given directory from its backups.
[[nodiscard]] patch_result restore_install(const fs::path& install_dir);

/// Same as above with a per-line log callback instead of a report buffer.
[[nodiscard]] patch_result patch_install(const fs::path& install_dir, const log_sink& on_line);

/// Same as above with a per-line log callback instead of a report buffer.
[[nodiscard]] patch_result restore_install(const fs::path& install_dir, const log_sink& on_line);

/// Current value of the embedded-ASAR-integrity fuse, or empty when absent.
[[nodiscard]] std::string fuse_state_of(const fs::path& exe);

/// Offset of the Electron fuse wire inside raw bytes, or npos when absent.
[[nodiscard]] std::size_t find_fuse_wire(const std::string& data) noexcept;
} // namespace wemod
