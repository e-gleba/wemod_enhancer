#pragma once

// Archive layer: pickle math, extract/repack, SHA-256 integrity, safe paths.
// Owns nothing public except pack/extract entry points used by patcher.cpp.

#include <nlohmann/json.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace wemod
{
namespace fs = std::filesystem;

using ordered_json = nlohmann::ordered_json;

/// Parsed ASAR header plus the body offset past the pickles.
struct header_view final
{
    ordered_json header;
    std::size_t data_offset{0UZ};
};

/// header_view plus raw bytes, parsed together so both stay in sync.
struct parsed_asar final
{
    header_view view;
    std::string raw;
};

/// Parse size pickle, header pickle and header JSON. Errors name the layer.
[[nodiscard]] std::expected<parsed_asar, std::string> parse_asar(const std::string& raw);

/// Flatten the files tree into (path, entry) pairs, depth-first, in order.
void collect_entries(ordered_json& node, const std::string& prefix,
                     std::vector<std::pair<std::string, ordered_json*>>& out);

/// Same for read-only headers.
void collect_entries(const ordered_json& node, const std::string& prefix,
                     std::vector<std::pair<std::string, const ordered_json*>>& out);

/// Entry predicates.
[[nodiscard]] bool is_link(const ordered_json& meta);
[[nodiscard]] bool is_unpacked(const ordered_json& meta);

/// Join root with a header path, rejecting escapes. Errors carry the path.
[[nodiscard]] std::expected<fs::path, std::string> safe_join(const fs::path& root, const std::string& rel);

/// Extract payload plus unpacked sidecars into out_dir.
[[nodiscard]] std::expected<std::monostate, std::string> extract_asar(const fs::path& asar, const fs::path& out_dir,
                                                                      ordered_json& header, std::size_t data_offset);

/// Rebuild sizes, integrity and offsets, then serialize the archive bytes.
[[nodiscard]] std::expected<std::string, std::string> pack_asar(const fs::path& root, ordered_json& header);

/// Whole-file SHA-256 plus 4 MiB block hashes, mirroring Python hashlib reads.
[[nodiscard]] std::expected<ordered_json, std::string> integrity_of(const std::string& data);

/// Lowercase hex SHA-256 of bytes.
/// Origin: https://github.com/okdshin/PicoSHA2 (MIT, pin 161cb3f).
[[nodiscard]] std::string sha256_hex(const std::string& data);

/// Read a whole binary file into data. Empty return means success.
[[nodiscard]] std::string read_file_bytes(const fs::path& path, std::string& data, std::string& error);

/// Write bytes, truncating. False plus error on failure.
[[nodiscard]] bool write_file_bytes(const fs::path& path, const std::string& data, std::string& error);

/// Copy with overwrite. Error names both ends.
[[nodiscard]] std::expected<std::monostate, std::string> copy_file_over(const fs::path& from, const fs::path& to);
} // namespace wemod
