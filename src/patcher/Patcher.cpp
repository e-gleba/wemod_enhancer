#include "Patcher.hpp"

#include <nlohmann/json.hpp>

#include <picosha2.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>

namespace wemod
{
namespace
{
using ordered_json = nlohmann::ordered_json;

constexpr std::size_t block_size{4UZ * 1024UZ * 1024UZ};
constexpr std::string_view fuse_sentinel{"dL7pKGdnNz796PbbjQWNKmHXBZaB9tsX"};
constexpr std::size_t fuse_sentinel_len{32UZ};
constexpr std::size_t fuse_index{4UZ};
constexpr char fuse_off{'0'};
constexpr char fuse_on{'1'};
constexpr char fuse_removed{'r'};

struct collector final
{
    std::vector<std::string> lines;
    log_sink on_line;
    bool has_sink{false};
};

void emit_line(collector& out, const std::string& line)
{
    out.lines.push_back(line);
    if (out.has_sink) {
        out.on_line(line);
    }
}

patch_error fail(const std::string& message)
{
    return patch_error{.message = message};
}

[[nodiscard]] std::string read_file_bytes(const fs::path& path, std::string& data, std::string& error)
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

[[nodiscard]] bool write_file_bytes(const fs::path& path, const std::string& data, std::string& error)
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

[[nodiscard]] std::uint32_t load_u32_le(const std::string& data, const std::size_t pos, bool& ok) noexcept
{
    if (pos + 4UZ > data.size()) {
        ok = false;
        return 0U;
    }
    const auto* bytes{reinterpret_cast<const unsigned char*>(data.data() + pos)};
    ok = true;
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

void store_u32_le(std::string& out, const std::uint32_t value)
{
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<char>((value >> 24U) & 0xFFU));
}

struct header_view final
{
    ordered_json header;
    std::size_t data_offset{0UZ};
};

[[nodiscard]] std::expected<header_view, std::string> read_asar_header(const std::string& raw)
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
    const std::string text{raw.substr(16UZ, json_size)};
    try {
        ordered_json header = ordered_json::parse(text);
        return header_view{.header = std::move(header), .data_offset = 8UZ + header_size};
    } catch (const std::exception&) {
        return std::unexpected<std::string>{"invalid ASAR header JSON"};
    }
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

void walk_entries(ordered_json& node, const std::string& prefix, std::vector<std::pair<std::string, ordered_json*>>& out)
{
    auto files_it{node.find("files")};
    if (files_it == node.end() || !files_it->is_object()) {
        return;
    }
    for (auto& item : files_it->items()) {
        const std::string rel{prefix.empty() ? item.key() : prefix + "/" + item.key()};
        ordered_json& value{item.value()};
        if (value.contains("files")) {
            walk_entries(value, rel, out);
        } else {
            out.emplace_back(rel, &value);
        }
    }
}

void walk_entries_const(const ordered_json& node, const std::string& prefix,
                        std::vector<std::pair<std::string, const ordered_json*>>& out)
{
    auto files_it{node.find("files")};
    if (files_it == node.end() || !files_it->is_object()) {
        return;
    }
    for (const auto& item : files_it->items()) {
        const std::string rel{prefix.empty() ? item.key() : prefix + "/" + item.key()};
        const ordered_json& value{item.value()};
        if (value.contains("files")) {
            walk_entries_const(value, rel, out);
        } else {
            out.emplace_back(rel, &value);
        }
    }
}

[[nodiscard]] bool is_link(const ordered_json& meta)
{
    return meta.contains("link");
}

[[nodiscard]] bool is_unpacked(const ordered_json& meta)
{
    const auto it{meta.find("unpacked")};
    return it != meta.end() && it->is_boolean() && it->get<bool>();
}

// Lexical containment: reject empty/dot/dotdot/absolute segments before
// joining, then verify the joined path stays under root by components.
[[nodiscard]] std::expected<fs::path, std::string> safe_join(const fs::path& root, const std::string& rel)
{
    if (rel.empty()) {
        return std::unexpected<std::string>{"unsafe ASAR path: " + rel};
    }
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : rel) {
        if (ch == '/') {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    parts.push_back(current);
    for (const std::string& part : parts) {
        if (part.empty() || part == "." || part == "..") {
            return std::unexpected<std::string>{"unsafe ASAR path: " + rel};
        }
        if (!part.empty() && (part.front() == '/' || part.front() == '\\')) {
            return std::unexpected<std::string>{"unsafe ASAR path: " + rel};
        }
    }
    if (!rel.empty() && (rel.front() == '/' || rel.front() == '\\')) {
        return std::unexpected<std::string>{"unsafe ASAR path: " + rel};
    }
    fs::path out{root};
    for (const std::string& part : parts) {
        out /= part;
    }
    // Component-wise containment: sibling-prefix safe (/tmp/root2 check).
    std::error_code error;
    const fs::path canon_root{fs::weakly_canonical(root, error)};
    if (error) {
        return std::unexpected<std::string>{"unsafe ASAR path: " + rel};
    }
    fs::path canon_out{canon_root};
    for (const std::string& part : parts) {
        canon_out /= part;
    }
    canon_out = canon_out.lexically_normal();
    auto root_it{canon_root.begin()};
    auto out_it{canon_out.begin()};
    for (; root_it != canon_root.end(); ++root_it, ++out_it) {
        if (out_it == canon_out.end() || *out_it != *root_it) {
            return std::unexpected<std::string>{"unsafe ASAR path: " + rel};
        }
    }
    return out;
}

[[nodiscard]] std::string sha256_hex(const std::string& data)
{
    // PicoSHA2 input iterators must be unsigned bytes: std::string's
    // signed char range hashes wrong bytes without this conversion.
    // Origin: https://github.com/okdshin/PicoSHA2 (MIT, pin 161cb3f).
    const std::vector<unsigned char> bytes{data.begin(), data.end()};
    std::vector<unsigned char> digest(picosha2::k_digest_size, 0);
    picosha2::hash256(bytes.begin(), bytes.end(), digest.begin(), digest.end());
    return picosha2::bytes_to_hex_string(digest);
}

[[nodiscard]] std::expected<ordered_json, std::string> integrity_of(const std::string& data)
{
    try {
        ordered_json node;
        node["algorithm"] = "SHA256";
        node["hash"] = sha256_hex(data);
        node["blockSize"] = block_size;
        ordered_json blocks = ordered_json::array();
        if (data.empty()) {
            // Python hashlib over zero reads: whole digest of empty, no blocks.
            node["blocks"] = blocks;
            return node;
        }
        for (std::size_t pos{0UZ}; pos < data.size(); pos += block_size) {
            const std::size_t len{std::min(block_size, data.size() - pos)};
            std::string chunk{data.substr(pos, len)};
            blocks.push_back(sha256_hex(chunk));
        }
        node["blocks"] = std::move(blocks);
        return node;
    } catch (const std::exception& error) {
        return std::unexpected<std::string>{error.what()};
    }
}

[[nodiscard]] std::expected<std::monostate, std::string> extract_asar(const fs::path& asar, const fs::path& out_dir,
                                                                      ordered_json& header, std::size_t data_offset)
{
    const fs::path unpacked{fs::path{asar.string() + ".unpacked"}};
    std::string raw;
    std::string error;
    if (!read_file_bytes(asar, raw, error).empty()) {
        return std::unexpected<std::string>{error};
    }
    std::vector<std::pair<std::string, ordered_json*>> items;
    walk_entries(header, {}, items);
    for (const auto& [rel, meta] : items) {
        if (is_link(*meta)) {
            continue;
        }
        auto dst{ safe_join(out_dir, rel) };

        if (!dst) {
            return std::unexpected<std::string>{dst.error()};
        }
        std::error_code code;
        fs::create_directories(dst->parent_path(), code);
        if (code) {
            return std::unexpected<std::string>{"cannot create " + dst->parent_path().string()};
        }
        if (is_unpacked(*meta)) {
            auto src{ safe_join(unpacked, rel) };
            if (!src) {
                return std::unexpected<std::string>{src.error()};
            }
            std::error_code probe;
            if (!fs::is_regular_file(*src, probe) || probe) {
                return std::unexpected<std::string>{"missing unpacked ASAR file: " + rel};
            }
            std::string bytes;
            if (!read_file_bytes(*src, bytes, error).empty()) {
                return std::unexpected<std::string>{error};
            }
            if (!write_file_bytes(*dst, bytes, error)) {
                return std::unexpected<std::string>{error};
            }
            continue;
        }
        const auto off_it{meta->find("offset")};
        const auto size_it{meta->find("size")};
        if (off_it == meta->end() || size_it == meta->end()) {
            return std::unexpected<std::string>{"invalid ASAR entry: " + rel};
        }
        std::size_t offset{0UZ};
        std::size_t size{0UZ};
        try {
            const std::string off_text{off_it->is_string() ? off_it->get<std::string>() : std::to_string(off_it->get<std::int64_t>())};
            offset = static_cast<std::size_t>(std::stoull(off_text));
            size = size_it->is_number_unsigned() ? static_cast<std::size_t>(size_it->get<std::uint64_t>())
                                                 : static_cast<std::size_t>(std::stoull(size_it->get<std::string>()));
        } catch (const std::exception&) {
            return std::unexpected<std::string>{"invalid ASAR entry: " + rel};
        }
        const std::size_t begin{data_offset + offset};
        if (begin + size > raw.size()) {
            return std::unexpected<std::string>{"invalid ASAR entry: " + rel};
        }
        if (!write_file_bytes(*dst, raw.substr(begin, size), error)) {
            return std::unexpected<std::string>{error};
        }
    }
    return std::monostate{};
}

[[nodiscard]] std::expected<std::string, std::string> pack_asar(const fs::path& root, ordered_json& header)
{
    // Recompute sizes, integrity, offsets in header traversal order, then
    // serialize compactly and rebuild pickles with 4-byte alignment.
    std::vector<std::string> packed_bodies;
    std::vector<std::pair<std::string, ordered_json*>> items;
    walk_entries(header, {}, items);
    std::size_t offset{0UZ};
    for (const auto& [rel, meta] : items) {
        if (is_link(*meta)) {
            continue;
        }
        auto src{ safe_join(root, rel) };
        if (!src) {
            return std::unexpected<std::string>{src.error()};
        }
        std::string bytes;
        std::string error;
        std::error_code probe;
        if (!fs::is_regular_file(*src, probe) || probe) {
            return std::unexpected<std::string>{"missing ASAR file: " + rel};
        }
        if (!read_file_bytes(*src, bytes, error).empty()) {
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
        packed_bodies.push_back(std::move(bytes));
        offset += packed_bodies.back().size();
    }
    std::string raw;
    try {
        raw = header.dump(-1, ' ', false, ordered_json::error_handler_t::strict);
    } catch (const std::exception& error) {
        return std::unexpected<std::string>{error.what()};
    }
    std::string out;
    out += size_pickle_bytes(static_cast<std::uint32_t>(pickle_payload(raw).size()));
    out += pickle_payload(raw);
    for (const std::string& body : packed_bodies) {
        out += body;
    }
    // Verify final size by reparsing and summing non-link, non-unpacked sizes.
    auto check{read_asar_header(out)};
    if (!check) {
        return std::unexpected<std::string>{"ASAR verification failed: archive size mismatch"};
    }
    std::vector<std::pair<std::string, const ordered_json*>> verify;
    walk_entries_const(check->header, {}, verify);
    std::size_t expect{check->data_offset};
    for (const auto& [rel, meta] : verify) {
        (void)rel;
        if (is_link(*meta) || is_unpacked(*meta)) {
            continue;
        }
        const auto it{meta->find("size")};
        if (it == meta->end()) {
            return std::unexpected<std::string>{"ASAR verification failed: archive size mismatch"};
        }
        std::size_t size{0UZ};
        try {
            size = it->is_number_unsigned() ? static_cast<std::size_t>(it->get<std::uint64_t>())
                                            : static_cast<std::size_t>(std::stoull(it->get<std::string>()));
        } catch (const std::exception&) {
            return std::unexpected<std::string>{"ASAR verification failed: archive size mismatch"};
        }
        expect += size;
    }
    if (out.size() != expect) {
        return std::unexpected<std::string>{"ASAR verification failed: archive size mismatch"};
    }
    return out;
}

struct patch_spec final
{
    std::string name;
    std::string pattern;
    bool needs_format{false};
    std::string literal;
    bool required{true};
};

// ECMAScript translations of the Python patterns. Python re.S lets an
// unescaped dot cross newlines; each such dot becomes [\s\S]. Named
// groups become positional groups in declaration order.
[[nodiscard]] const std::vector<patch_spec>& patch_specs()
{
    static const std::vector<patch_spec> specs{
        {.name = "activate-pro-account",
         .pattern = "getUserAccount\\(\\)\\{[\\s\\S]*?return\\s+this\\.\\#([\\w$]+)\\.fetch\\(\\{[\\s\\S]*?\\}\\)\\}",
         .needs_format = true,
         .literal = {},
         .required = true},
        {.name = "activate-pro-language",
         .pattern = "setAccountLanguage\\(([^)]*)\\)\\{\\s*return\\s+(this\\.\\#\\w+\\.post\\(\"/v3/account/language\",\\{[^}]*\\}\\))\\s*;?\\s*\\}",
         .needs_format = true,
         .literal = {},
         .required = true},
        {.name = "disable-native-pairing",
         .pattern = "requestRemoteAuthCode\\(\\)\\{return this\\.\\#[\\w$]+\\.post\\(\"/v3/auth/remote_code\"\\)\\}",
         .needs_format = false,
         .literal = "requestRemoteAuthCode(){return Promise.reject(new Error(\"wemod-enhancer: native mobile pairing "
                   "disabled\"))}",
         .required = true},
        {.name = "disable-updates",
         .pattern = "registerHandler\\(\"ACTION_CHECK_FOR_UPDATE\"[\\s\\S]*?\\)\\)\\)\\)",
         .needs_format = false,
         .literal = "registerHandler(\"ACTION_CHECK_FOR_UPDATE\",(e=>expectUpdateFeedUrl(e,(e=>null)))",
         .required = true},
        {.name = "devtools-f12",
         .pattern = "(\\w+)\\.whenReady\\(\\)\\.then\\(",
         .needs_format = true,
         .literal = {},
         .required = true},
        {.name = "activate-pro-brand",
         .pattern = "setAccountWandBrandExperience\\(\\)\\{[\\s\\S]*?return\\s+this\\.\\#([\\w$]+)\\.post\\(\"/v3/account/"
                   "brand_experience_wand\"\\)\\}",
         .needs_format = true,
         .literal = {},
         .required = false},
    };
    return specs;
}

[[nodiscard]] std::string apply_patch(const patch_spec& spec, const std::smatch& match)
{
    // Replacement text matches the Python lambdas exactly, including
    // capture substitution.
    if (!spec.needs_format) {
        return spec.literal;
    }
    if (spec.name == "activate-pro-account") {
        // Python f-string {{/}} escapes mean single braces here.
        const std::string part{match[1].str()};
        return "getUserAccount(){return this.#" + part +
               ".fetch({endpoint:\"/v3/account\",method:\"GET\",name:\"/v3/account\",collectMetrics:0}).then("
               "response=>{response.subscription={period:\"yearly\",state:\"active\"};return response;})}";
    }
    if (spec.name == "activate-pro-language") {
        const std::string param{match[1].str()};
        const std::string expr{match[2].str()};
        return "setAccountLanguage(" + param + "){return (" + expr +
               ").then(response=>{response&&\"object\"==typeof response&&(response.subscription={period:\"yearly\","
               "state:\"active\"});return response;})}";
    }
    if (spec.name == "devtools-f12") {
        const std::string app{match[1].str()};
        return app + ".on(\"browser-window-created\",((_,w)=>{try{w.webContents.on(\"before-input-event\",((_,i)=>{if("
                      "\"F12\"===i.key&&\"keyDown\"===i.type){w.webContents.isDevToolsOpened()?w.webContents."
                      "closeDevTools():w.webContents.openDevTools({mode:\"detach\"})}}))}catch(e){}}))," +
               app + ".whenReady().then(";
    }
    if (spec.name == "activate-pro-brand") {
        const std::string part{match[1].str()};
        return "setAccountWandBrandExperience(){return this.#" + part +
               ".post(\"/v3/account/brand_experience_wand\").then(response=>{response.subscription={period:\"yearly\","
               "state:\"active\"};return response;})}";
    }
    return {};
}

[[nodiscard]] std::size_t count_matches(const std::regex& expr, const std::string& text)
{
    std::size_t count{0UZ};
    auto begin{text.cbegin()};
    const auto end{text.cend()};
    std::smatch match;
    while (std::regex_search(begin, end, match, expr)) {
        ++count;
        begin = match.suffix().first;
        if (begin == end) {
            break;
        }
    }
    return count;
}

[[nodiscard]] std::expected<std::monostate, std::string> patch_bundles(const fs::path& root, collector& out)
{
    std::vector<fs::path> candidates;
    std::error_code error;
    for (fs::directory_iterator it{root, error}, end; it != end && !error; it.increment(error)) {
        std::error_code probe;
        if (!it->is_regular_file(probe) || probe) {
            continue;
        }
        const std::string name{it->path().filename().string()};
        const bool is_index{name == "index.js"};
        const bool is_bundle{name.size() > 12UZ && name.starts_with("app-") && name.ends_with(".bundle.js")};
        if (is_index || is_bundle) {
            candidates.push_back(it->path());
        }
    }
    if (error) {
        return std::unexpected<std::string>{"cannot list " + root.string()};
    }
    std::sort(candidates.begin(), candidates.end());
    std::vector<patch_spec> pending;
    std::vector<patch_spec> optional;
    for (const patch_spec& spec : patch_specs()) {
        if (spec.required) {
            pending.push_back(spec);
        } else {
            optional.push_back(spec);
        }
    }
    std::vector<std::regex> pending_expr;
    std::vector<std::regex> optional_expr;
    try {
        for (const patch_spec& spec : pending) {
            pending_expr.emplace_back(spec.pattern, std::regex::ECMAScript);
        }
        for (const patch_spec& spec : optional) {
            optional_expr.emplace_back(spec.pattern, std::regex::ECMAScript);
        }
    } catch (const std::regex_error& failure) {
        return std::unexpected<std::string>{failure.what()};
    }
    for (const fs::path& path : candidates) {
        std::string text;
        std::string io_error;
        if (!read_file_bytes(path, text, io_error).empty()) {
            return std::unexpected<std::string>{io_error};
        }

        bool changed{false};
        for (std::size_t i{0UZ}; i < pending.size();) {
            const std::size_t found{count_matches(pending_expr[i], text)};
            if (found == 0UZ) {
                ++i;
                continue;
            }
            if (found != 1UZ) {
                return std::unexpected<std::string>{pending[i].name + ": expected one match, got " + std::to_string(found)};
            }
            std::smatch match;
            if (!std::regex_search(text, match, pending_expr[i])) {
                return std::unexpected<std::string>{pending[i].name + ": expected one match, got 0"};
            }
            const std::string replacement{apply_patch(pending[i], match)};
            text = std::regex_replace(text, pending_expr[i], replacement, std::regex_constants::format_first_only);
            changed = true;
            emit_line(out, "patched " + pending[i].name + ": " + path.filename().string());
            pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
            pending_expr.erase(pending_expr.begin() + static_cast<std::ptrdiff_t>(i));
        }
        for (std::size_t i{0UZ}; i < optional.size();) {
            const std::size_t found{count_matches(optional_expr[i], text)};
            if (found == 0UZ) {
                ++i;
                continue;
            }
            if (found != 1UZ) {
                return std::unexpected<std::string>{optional[i].name + ": expected one match, got " + std::to_string(found)};
            }
            std::smatch match;
            if (!std::regex_search(text, match, optional_expr[i])) {
                return std::unexpected<std::string>{optional[i].name + ": expected one match, got 0"};
            }
            const std::string replacement{apply_patch(optional[i], match)};
            text = std::regex_replace(text, optional_expr[i], replacement, std::regex_constants::format_first_only);
            changed = true;
            emit_line(out, "patched " + optional[i].name + ": " + path.filename().string());
            optional.erase(optional.begin() + static_cast<std::ptrdiff_t>(i));
            optional_expr.erase(optional_expr.begin() + static_cast<std::ptrdiff_t>(i));
        }
        if (changed) {
            if (!write_file_bytes(path, text, io_error)) {
                return std::unexpected<std::string>{io_error};
            }
        }
    }
    for (const patch_spec& spec : optional) {
        emit_line(out, "skipped optional patch " + spec.name + ": endpoint not present in this client");
    }
    if (!pending.empty()) {
        std::string names;
        for (std::size_t i{0UZ}; i < pending.size(); ++i) {
            names += pending[i].name;
            if (i + 1UZ < pending.size()) {
                names += ", ";
            }
        }
        return std::unexpected<std::string>{"unsupported client; missing patches: " + names};
    }
    return std::monostate{};
}

[[nodiscard]] std::size_t fuse_wire_of(const std::string& data) noexcept
{
    std::size_t start{0UZ};
    while (true) {
        const std::size_t off{data.find(fuse_sentinel, start)};
        if (off == std::string::npos) {
            return std::string::npos;
        }
        if (off + 34UZ <= data.size() && data[off + 32UZ] == 1 && static_cast<unsigned char>(data[off + 33UZ]) >= 5U) {
            return off;
        }
        start = off + 1UZ;
        if (start >= data.size()) {
            return std::string::npos;
        }
    }
}

[[nodiscard]] std::expected<char, std::string> fuse_state_bytes(const std::string& data)
{
    const std::size_t off{fuse_wire_of(data)};
    if (off == std::string::npos) {
        return std::unexpected<std::string>{"no fuse wire"};
    }
    return data[off + 34UZ + fuse_index];
}

[[nodiscard]] std::expected<char, std::string> read_fuse_state(const fs::path& exe)
{
    std::string data;
    std::string error;
    if (!read_file_bytes(exe, data, error).empty()) {
        return std::unexpected<std::string>{error};
    }
    auto state{fuse_state_bytes(data)};
    if (!state) {
        return std::unexpected<std::string>{state.error()};
    }
    return *state;
}

[[nodiscard]] std::expected<char, std::string> write_fuse_state(const fs::path& exe, const char state)
{
    if (state != fuse_off && state != fuse_on && state != fuse_removed) {
        return std::unexpected<std::string>{std::string{"invalid fuse state '"} + state + "'"};
    }
    std::string data;
    std::string error;
    if (!read_file_bytes(exe, data, error).empty()) {
        return std::unexpected<std::string>{error};
    }
    const std::size_t off{fuse_wire_of(data)};
    if (off == std::string::npos) {
        return std::unexpected<std::string>{"electron fuse wire not found in " + exe.string()};
    }
    const std::size_t pos{off + 34UZ + fuse_index};
    const char previous{data[pos]};
    if (previous != fuse_off && previous != fuse_on && previous != fuse_removed) {
        return std::unexpected<std::string>{std::string{"unexpected fuse value '"} + previous + "' in " + exe.string()};
    }
    if (previous == state) {
        return previous;
    }
    data[pos] = state;
    if (!write_file_bytes(exe, data, error)) {
        return std::unexpected<std::string>{error};
    }
    return previous;
}

[[nodiscard]] std::optional<fs::path> electron_exe(const fs::path& install)
{
    for (const std::string& name : {std::string{"Wand.exe"}, std::string{"WeMod.exe"}}) {
        const fs::path candidate{install / name};
        std::error_code probe;
        if (fs::is_regular_file(candidate, probe) && !probe) {
            return candidate;
        }
    }
    return std::nullopt;
}

[[nodiscard]] fs::path exe_backup_path(const fs::path& exe)
{
    return fs::path{exe.string() + ".wemod-enhancer-backup"};
}

void cleanup_legacy_dll(const fs::path& install, collector& out)
{
    const fs::path dll{install / "version.dll"};
    const fs::path dll_backup{install / "version.dll.wemod-enhancer-backup"};
    std::error_code probe;
    if (fs::is_regular_file(dll_backup, probe) && !probe) {
        std::error_code copy_error;
        fs::copy_file(dll_backup, dll, fs::copy_options::overwrite_existing, copy_error);
        std::error_code remove_error;
        fs::remove(dll_backup, remove_error);
        emit_line(out, "restored pre-proxy version.dll");
        return;
    }
    probe.clear();
    if (fs::is_regular_file(dll, probe) && !probe) {
        emit_line(out, "kept unexpected version.dll - remove manually if it is the retired proxy");
    }
}

[[nodiscard]] std::expected<bool, std::string> flip_fuse(const fs::path& exe, collector& out)
{
    auto state{read_fuse_state(exe)};
    if (!state) {
        emit_line(out, "skipped fuse flip: no electron fuse wire in " + exe.filename().string());
        return false;
    }
    if (*state == fuse_removed) {
        emit_line(out, "asar integrity fuse already removed in " + exe.filename().string());
        return false;
    }
    const fs::path backup{exe_backup_path(exe)};
    std::error_code copy_error;
    fs::copy_file(exe, backup, fs::copy_options::overwrite_existing, copy_error);
    if (copy_error) {
        return std::unexpected<std::string>{"cannot back up " + exe.string()};
    }
    auto previous{write_fuse_state(exe, fuse_removed)};
    if (!previous) {
        return std::unexpected<std::string>{previous.error()};
    }
    emit_line(out,
              "removed asar integrity fuse in " + exe.filename().string() + " (was '" + std::string(1, *previous) + "')");
    return true;
}

void restore_fuse(const fs::path& exe, collector& out)
{
    const fs::path backup{exe_backup_path(exe)};
    const fs::path legacy{exe.parent_path() / "electron-fuse.wemod-enhancer-backup"};
    std::error_code probe;
    if (fs::is_regular_file(backup, probe) && !probe) {
        std::error_code copy_error;
        fs::copy_file(backup, exe, fs::copy_options::overwrite_existing, copy_error);
        std::error_code remove_error;
        fs::remove(backup, remove_error);
        emit_line(out, "restored " + exe.filename().string() + " from backup");
        return;
    }
    probe.clear();
    if (fs::is_regular_file(legacy, probe) && !probe) {
        std::string stored;
        std::string error;
        bool usable{false};
        char value{fuse_off};
        if (read_file_bytes(legacy, stored, error).empty()) {
            std::string trimmed;
            for (const char ch : stored) {
                if (!std::isspace(static_cast<unsigned char>(ch))) {
                    trimmed.push_back(ch);
                }
            }
            auto current{read_fuse_state(exe)};
            if (!trimmed.empty() && current && *current == fuse_removed) {
                value = trimmed.front();
                usable = true;
            }
        }
        if (usable) {
            auto result{write_fuse_state(exe, value)};
            if (result) {
                emit_line(out, "restored asar integrity fuse in " + exe.filename().string() + " (was '" +
                                   std::string(1, value) + "')");
            }
        }
        std::error_code remove_error;
        fs::remove(legacy, remove_error);
    }
}

[[nodiscard]] std::expected<std::monostate, std::string> copy_file_over(const fs::path& from, const fs::path& to)
{
    std::error_code copy_error;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, copy_error);
    if (copy_error) {
        return std::unexpected<std::string>{"cannot copy " + from.string() + " to " + to.string()};
    }
    return std::monostate{};
}

[[nodiscard]] patch_result run_patch(const fs::path& install, collector& out)
{
    const fs::path resources{install / "resources"};
    const fs::path asar{resources / "app.asar"};
    const fs::path backup{resources / "app.asar.backup"};
    std::error_code probe;
    if (!fs::is_regular_file(asar, probe) || probe) {
        return std::unexpected<patch_error>{fail("missing " + asar.string())};
    }
    probe.clear();
    if (!fs::exists(backup, probe) || probe) {
        auto created{copy_file_over(asar, backup)};
        if (!created) {
            return std::unexpected<patch_error>{fail(created.error())};
        }
    } else {
        auto reset{copy_file_over(backup, asar)};
        if (!reset) {
            return std::unexpected<patch_error>{fail(reset.error())};
        }
    }
    const std::optional<fs::path> exe{electron_exe(install)};
    bool exe_touched{false};
    try {
        std::error_code temp_error;
        const fs::path anchor{fs::temp_directory_path(temp_error)};
        if (temp_error) {
            return std::unexpected<patch_error>{fail("cannot locate temp dir")};
        }
        const std::string base{(anchor / "wemod-enhancer-XXXXXX").string()};
        std::string dir_template{base};
        dir_template.push_back('\0');
        if (mkdtemp(dir_template.data()) == nullptr) {
            return std::unexpected<patch_error>{fail("cannot create temp dir")};
        }
        dir_template.pop_back();
        const fs::path work_path{dir_template};
        auto cleanup{[&]() {
            std::error_code remove_error;
            fs::remove_all(work_path, remove_error);
        }};
        std::string raw;
        std::string error;
        if (!read_file_bytes(asar, raw, error).empty()) {
            cleanup();
            return std::unexpected<patch_error>{fail(error)};
        }
        auto parsed{read_asar_header(raw)};
        if (!parsed) {
            cleanup();
            return std::unexpected<patch_error>{fail(parsed.error())};
        }
        auto extracted{extract_asar(asar, work_path, parsed->header, parsed->data_offset)};
        if (!extracted) {
            cleanup();
            auto rolled{copy_file_over(backup, asar)};
            (void)rolled;
            if (exe && exe_touched) {
                restore_fuse(*exe, out);
            }
            return std::unexpected<patch_error>{fail(extracted.error())};
        }
        auto patched{patch_bundles(work_path, out)};
        if (!patched) {
            cleanup();
            auto rolled{copy_file_over(backup, asar)};
            (void)rolled;
            if (exe && exe_touched) {
                restore_fuse(*exe, out);
            }
            return std::unexpected<patch_error>{fail(patched.error())};
        }
        auto packed{pack_asar(work_path, parsed->header)};
        if (!packed) {
            cleanup();
            auto rolled{copy_file_over(backup, asar)};
            (void)rolled;
            if (exe && exe_touched) {
                restore_fuse(*exe, out);
            }
            return std::unexpected<patch_error>{fail(packed.error())};
        }
        const fs::path tmp_out{fs::path{asar.string() + ".tmp"}};
        if (!write_file_bytes(tmp_out, *packed, error)) {
            cleanup();
            auto rolled{copy_file_over(backup, asar)};
            (void)rolled;
            if (exe && exe_touched) {
                restore_fuse(*exe, out);
            }
            return std::unexpected<patch_error>{fail(error)};
        }
        std::error_code rename_error;
        fs::rename(tmp_out, asar, rename_error);
        if (rename_error) {
            std::error_code remove_error;
            fs::remove(tmp_out, remove_error);
            cleanup();
            auto rolled{copy_file_over(backup, asar)};
            (void)rolled;
            if (exe && exe_touched) {
                restore_fuse(*exe, out);
            }
            return std::unexpected<patch_error>{fail("cannot replace " + asar.string())};
        }
        cleanup();
        cleanup_legacy_dll(install, out);
        if (exe) {
            auto flipped{flip_fuse(*exe, out)};
            if (!flipped) {
                auto rolled{copy_file_over(backup, asar)};
                (void)rolled;
                return std::unexpected<patch_error>{fail(flipped.error())};
            }
            exe_touched = *flipped;
        } else {
            emit_line(out, "skipped fuse flip: no Wand.exe/WeMod.exe next to resources");
        }
        emit_line(out, "patched " + install.string());
    } catch (const std::exception& error) {
        auto rolled{copy_file_over(backup, asar)};
        (void)rolled;
        if (exe && exe_touched) {
            restore_fuse(*exe, out);
        }
        return std::unexpected<patch_error>{fail(error.what())};
    }
    return patch_report{.install_dir = install, .log_lines = out.lines, .fuse_removed = exe_touched};
}

[[nodiscard]] patch_result run_restore(const fs::path& install, collector& out)
{
    const fs::path resources{install / "resources"};
    const fs::path asar{resources / "app.asar"};
    const fs::path backup{resources / "app.asar.backup"};
    std::error_code probe;
    if (!fs::exists(backup, probe) || probe) {
        return std::unexpected<patch_error>{fail("missing " + backup.string())};
    }
    auto copied{copy_file_over(backup, asar)};
    if (!copied) {
        return std::unexpected<patch_error>{fail(copied.error())};
    }
    cleanup_legacy_dll(install, out);
    if (const std::optional<fs::path> exe{electron_exe(install)}) {
        restore_fuse(*exe, out);
    }
    emit_line(out, "restored " + install.string());
    return patch_report{.install_dir = install, .log_lines = out.lines, .fuse_removed = false};
}

} // namespace

std::size_t find_fuse_wire(const std::string& data) noexcept
{
    return fuse_wire_of(data);
}

std::string fuse_state_of(const fs::path& exe)
{
    auto state{read_fuse_state(exe)};
    if (!state) {
        return {};
    }
    return std::string(1, *state);
}

patch_result patch_install(const fs::path& install_dir)
{
    collector out;
    return run_patch(install_dir, out);
}

patch_result restore_install(const fs::path& install_dir)
{
    collector out;
    return run_restore(install_dir, out);
}

patch_result patch_install(const fs::path& install_dir, const log_sink& on_line)
{
    collector out;
    out.on_line = on_line;
    out.has_sink = static_cast<bool>(on_line);
    return run_patch(install_dir, out);
}

patch_result restore_install(const fs::path& install_dir, const log_sink& on_line)
{
    collector out;
    out.on_line = on_line;
    out.has_sink = static_cast<bool>(on_line);
    return run_restore(install_dir, out);
}

} // namespace wemod
