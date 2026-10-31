#pragma once

// Bundle layer: the six JavaScript patches as ECMAScript translations of the
// Python patterns, applied exactly once across index.js and app-*.bundle.js.

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <variant>

namespace wemod
{
namespace fs = std::filesystem;

/// Receives one log line per event, without trailing newline.
class line_sink
{
  public:
    virtual ~line_sink() = default;
    virtual void emit(const std::string& line) = 0;
};

/// Patch every bundle under root. Five patches required, one optional.
[[nodiscard]] std::expected<std::monostate, std::string> patch_bundles(const fs::path& root, line_sink& out);
} // namespace wemod
