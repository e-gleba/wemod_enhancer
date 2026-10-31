#include "asar.hpp"

#include <picosha2.h>

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <system_error>

namespace wemod
{
namespace
{
constexpr std::size_t block_size{4UZ * 1024UZ * 1024UZ};

[[nodiscard]] std::uint32_t load_u32_le(const std::string& data, const std::size_t pos, bool& ok) noexcept
{
    if (pos + 4UZ > data.size()) {
        ok = false;
        return 0U;
    }
    const auto* bytes{reinterpret_cast<const unsigned char*>(data.data() + pos)};
    ok = true;
    return static_cast<std::uint32_t>(bytes[0UZ]) | (static_cast<std::uint32_t>(bytes[1UZ]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2UZ]) << 16U) | (static_cast<std::uint32_t>(bytes[3UZ]) << 24U);
}

void store_u32_le(std::string& out, const std::uint32_t value)
{
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<char>((value >> 24U) & 0xFFU));
}

[[nodiscard]] std::string pickle_payload(const std::string& payload)
{
    std::string body;
    body.reserve(payload.size() + 8UZ);
    const std::uint32_t size{static_cast<std::uint32_t>(payload.size())};
    store_u32_le(body, size);
    body += payload;
    const std::size_t pad{(4UZ - (body.size() % 4UZ)) % 4UZ};
    body.append(pad, '\0');
    std::string out;
    out.reserve(body.size() + 4UZ);
    store_u32_le(out, static_cast<std::uint32_t>(body.size()));
    out += body;
    return out;
}

[[nodiscard]] std::string size_pickle_bytes(const std::uint32_t value)
{
    std::string out;
    store_u32_le(out, 4U);
    store_u32_le(out, value);
    return out;
}

void collect_into(auto& node, const std::string& prefix, auto& out)
{
    const auto files_it{node.find("files")};
    if (files_it == node.end() || !files_it->is_object()) {
        return;
    }
    for (auto& item : files_it->items()) {
        const std::string rel{prefix.empty() ? item.key() : prefix + "/" + item.key()};
        auto& value{item.value()};
        if (value.contains("files")) {
            collect_into(value, rel, out);
            continue;
        }
        out.emplace_back(rel, &value);
    }
}

[[nodiscard]] std::expected<std::size_t, std::string> entry_size(const ordered_json& meta, const std::string& rel)
{
    const auto it{meta.find("size")};
    if (it == meta.end()) {
        return std::unexpected<std::string>{"invalid ASAR entry: " + rel};
    }
    try {
        if (it->is_number_unsigned()) {
            return static_cast<std::size_t>(it->get<std::uint64_t>());
        }
        return static_cast<std::size_t>(std::stoull(it->get<std::string>()));
    } catch (const std::exception&) {
        return std::unexpected<std::string>{"invalid ASAR entry: " + rel};
    }
}

[[nodiscard]] std::expected<std::size_t, std::string> entry_offset(const ordered_json& meta, const std::string& rel)
{
    const auto it{meta.find("offset")};
    if (it == meta.end()) {
        return std::unexpected<std::string>{"invalid ASAR entry: " + rel};
    }
    try {
        const std::string text{it->is_string() ? it->get<std::string>() : std::to_string(it->get<std::int64_t>())};
        return static_cast<std::size_t>(std::stoull(text));
    } catch (const std::exception&) {
        return std::unexpected<std::string>{"invalid ASAR entry: " + rel};
    }
}

// Containment by components, not string prefix: /tmp/root stays distinct
// from /tmp/root2. Both sides are normalized before the comparison.
[[nodiscard]] bool under_root(const fs::path& root, const fs::path& candidate)
{
    const fs::path normal_root{root.lexically_normal()};
    const fs::path normal_candidate{candidate.lexically_normal()};
    auto root_it{normal_root.begin()};
    auto candidate_it{normal_candidate.begin()};
    for (; root_it != normal_root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == normal_candidate.end() || *candidate_it != *root_it) {
            return false;
        }
    }
    return true;
}
} // namespace

std::string read_file_bytes(const fs::path& path, std::string& data, std::string& error)
{
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        error = "cannot open " + path.string();
        return error;
    }
    std::ostringstream body;
    body << in.rdbuf();
    data = body.str();
    return {};
}

bool write_file_bytes(const fs::path& path, const std::string& data, std::string& error)
{
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    if (!out) {
        error = "cannot open " + path.string();
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

std::expected<std::monostate, std::string> copy_file_over(const fs::path& from, const fs::path& to)
{
    std::error_code copy_error;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, copy_error);
    if (copy_error) {
        return std::unexpected<std::string>{"cannot copy " + from.string() + " to " + to.string()};
    }
    return std::monostate{};
}

std::expected<parsed_asar, std::string> parse_asar(const std::string& raw)
{
    // Mirrors read_asar(): size pickle, header pickle, JSON bounds.
    bool ok{false};
    if (raw.size() < 8UZ) {
        return std::unexpected<std::string>{"invalid ASAR size pickle"};
    }
    const std::uint32_t magic{load_u32_le(raw, 0UZ, ok)};
    const std::uint32_t header_size{load_u32_le(raw, 4UZ, ok)};
    if (!ok || magic != 4U) {
        return std::unexpected<std::string>{"invalid ASAR size pickle"};
    }
    if (raw.size() < 8UZ + header_size || header_size < 8U) {
        return std::unexpected<std::string>{"invalid ASAR header pickle"};
    }
    const std::uint32_t payload_size{load_u32_le(raw, 8UZ, ok)};
    const std::uint32_t json_size{load_u32_le(raw, 12UZ, ok)};
    if (!ok) {
        return std::unexpected<std::string>{"invalid ASAR header pickle"};
    }
    if (payload_size + 4U != header_size || json_size > payload_size - 4U) {
        return std::unexpected<std::string>{"invalid ASAR header sizes"};
    }
    try {
        parsed_asar parsed{
            .view = {.header = ordered_json::parse(raw.substr(16UZ, json_size)), .data_offset = 8UZ + header_size},
            .raw = raw,
        };
        return parsed;
    } catch (const std::exception&) {
        return std::unexpected<std::string>{"invalid ASAR header JSON"};
    }
}

void collect_entries(ordered_json& node, const std::string& prefix,
                     std::vector<std::pair<std::string, ordered_json*>>& out)
{
    collect_into(node, prefix, out);
}

void collect_entries(const ordered_json& node, const std::string& prefix,
                     std::vector<std::pair<std::string, const ordered_json*>>& out)
{
    collect_into(node, prefix, out);
}

bool is_link(const ordered_json& meta)
{
    return meta.contains("link");
}

bool is_unpacked(const ordered_json& meta)
{
    const auto it{meta.find("unpacked")};
    return it != meta.end() && it->is_boolean() && it->get<bool>();
}

std::expected<fs::path, std::string> safe_join(const fs::path& root, const std::string& rel)
{
    if (rel.empty() || rel.front() == '/' || rel.front() == '\\') {
        return std::unexpected<std::string>{"unsafe ASAR path: " + rel};
    }
    std::vector<std::string> parts;
    std::string current;
    for (const char piece : rel) {
        if (piece == '/') {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(piece);
    }
    parts.push_back(current);
    const bool bad{std::ranges::any_of(parts, [](const std::string& part) {
        return part.empty() || part == "." || part == ".." || part.front() == '/' || part.front() == '\\';
    })};
    if (bad) {
        return std::unexpected<std::string>{"unsafe ASAR path: " + rel};
    }
    fs::path joined{root};
    for (const std::string& part : parts) {
        joined /= part;
    }
    std::error_code error;
    const fs::path canon_root{fs::weakly_canonical(root, error)};
    if (error) {
        return std::unexpected<std::string>{"unsafe ASAR path: " + rel};
    }
    fs::path canon_joined{canon_root};
    for (const std::string& part : parts) {
        canon_joined /= part;
    }
    if (!under_root(canon_root, canon_joined)) {
        return std::unexpected<std::string>{"unsafe ASAR path: " + rel};
    }
    return joined;
}

std::string sha256_hex(const std::string& data)
{
    // PicoSHA2 input iterators must be unsigned bytes: std::string's signed
    // char range hashes wrong bytes without this conversion.
    const std::vector<unsigned char> bytes{data.begin(), data.end()};
    std::vector<unsigned char> digest(picosha2::k_digest_size, 0UZ);
    picosha2::hash256(bytes.begin(), bytes.end(), digest.begin(), digest.end());
    return picosha2::bytes_to_hex_string(digest);
}

std::expected<ordered_json, std::string> integrity_of(const std::string& data)
{
    try {
        ordered_json node;
        node["algorithm"] = "SHA256";
        node["hash"] = sha256_hex(data);
        node["blockSize"] = block_size;
        ordered_json blocks = ordered_json::array();
        if (!data.empty()) {
            // Python hashlib over zero reads: whole digest of empty, no blocks.
            for (const auto chunk : data | std::views::chunk(block_size)) {
                blocks.push_back(sha256_hex(std::string{chunk.begin(), chunk.end()}));
            }
        }
        node["blocks"] = std::move(blocks);
        return node;
    } catch (const std::exception& failure) {
        return std::unexpected<std::string>{failure.what()};
    }
}

std::expected<std::monostate, std::string> extract_asar(const fs::path& asar, const fs::path& out_dir,
                                                        ordered_json& header, std::size_t data_offset)
{
    const fs::path unpacked{fs::path{asar.string() + ".unpacked"}};
    std::string raw;
    std::string error;
    if (!read_file_bytes(asar, raw, error).empty()) {
        return std::unexpected<std::string>{error};
    }
    std::vector<std::pair<std::string, ordered_json*>> items;
    collect_entries(header, {}, items);
    for (const auto& [rel, meta] : items) {
        if (is_link(*meta)) {
            continue;
        }
        auto destination{safe_join(out_dir, rel)};
        if (!destination) {
            return std::unexpected<std::string>{destination.error()};
        }
        std::error_code code;
        fs::create_directories(destination->parent_path(), code);
        if (code) {
            return std::unexpected<std::string>{"cannot create " + destination->parent_path().string()};
        }
        if (is_unpacked(*meta)) {
            auto source{safe_join(unpacked, rel)};
            if (!source) {
                return std::unexpected<std::string>{source.error()};
            }
            std::error_code probe;
            if (!fs::is_regular_file(*source, probe) || probe) {
                return std::unexpected<std::string>{"missing unpacked ASAR file: " + rel};
            }
            std::string bytes;
            if (!read_file_bytes(*source, bytes, error).empty()) {
                return std::unexpected<std::string>{error};
            }
            if (!write_file_bytes(*destination, bytes, error)) {
                return std::unexpected<std::string>{error};
            }
            continue;
        }
        auto offset{entry_offset(*meta, rel)};
        if (!offset) {
            return std::unexpected<std::string>{offset.error()};
        }
        auto size{entry_size(*meta, rel)};
        if (!size) {
            return std::unexpected<std::string>{size.error()};
        }
        const std::size_t begin{data_offset + *offset};
        if (begin + *size > raw.size()) {
            return std::unexpected<std::string>{"invalid ASAR entry: " + rel};
        }
        if (!write_file_bytes(*destination, raw.substr(begin, *size), error)) {
            return std::unexpected<std::string>{error};
        }
    }
    return std::monostate{};
}

std::expected<std::string, std::string> pack_asar(const fs::path& root, ordered_json& header)
{
    // Recompute sizes, integrity, offsets in header traversal order, then
    // serialize compactly and rebuild pickles with 4-byte alignment.
    std::vector<std::string> packed_bodies;
    std::vector<std::pair<std::string, ordered_json*>> items;
    collect_entries(header, {}, items);
    std::size_t offset{0UZ};
    for (const auto& [rel, meta] : items) {
        if (is_link(*meta)) {
            continue;
        }
        auto source{safe_join(root, rel)};
        if (!source) {
            return std::unexpected<std::string>{source.error()};
        }
        std::string bytes;
        std::string error;
        std::error_code probe;
        if (!fs::is_regular_file(*source, probe) || probe) {
            return std::unexpected<std::string>{"missing ASAR file: " + rel};
        }
        if (!read_file_bytes(*source, bytes, error).empty()) {
            return std::unexpected<std::string>{error};
        }
        (*meta)["size"] = bytes.size();
        auto integrity{integrity_of(bytes)};
        if (!integrity) {
            return std::unexpected<std::string>{integrity.error()};
        }
        (*meta)["integrity"] = std::move(*integrity);
        if (is_unpacked(*meta)) {
            meta->erase("offset");
            continue;
        }
        (*meta)["offset"] = std::to_string(offset);
        offset += bytes.size();
        packed_bodies.push_back(std::move(bytes));
    }
    std::string raw;
    try {
        raw = header.dump(-1, ' ', false, ordered_json::error_handler_t::strict);
    } catch (const std::exception& failure) {
        return std::unexpected<std::string>{failure.what()};
    }
    const std::string header_pickle{pickle_payload(raw)};
    std::string out;
    out += size_pickle_bytes(static_cast<std::uint32_t>(header_pickle.size()));
    out += header_pickle;
    for (const std::string& body : packed_bodies) {
        out += body;
    }
    // Verify final size by reparsing and summing packed sizes.
    auto check{parse_asar(out)};
    if (!check) {
        return std::unexpected<std::string>{"ASAR verification failed: archive size mismatch"};
    }
    std::vector<std::pair<std::string, const ordered_json*>> verify;
    collect_entries(check->view.header, {}, verify);
    std::size_t expect{check->view.data_offset};
    for (const auto& [rel, meta] : verify) {
        if (is_link(*meta) || is_unpacked(*meta)) {
            continue;
        }
        auto size{entry_size(*meta, rel)};
        if (!size) {
            return std::unexpected<std::string>{"ASAR verification failed: archive size mismatch"};
        }
        expect += *size;
    }
    if (out.size() != expect) {
        return std::unexpected<std::string>{"ASAR verification failed: archive size mismatch"};
    }
    return out;
}
} // namespace wemod
