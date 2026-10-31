// C++23 port of scripts/wemod_enhancer.py (fuse-wire variant).
// Public surface is pure std (patcher.hpp); nlohmann_json + Mbed-TLS stay
// in this translation unit. Behaviour mirrors the script 1:1.
// Ref: scripts/wemod_enhancer.py
// Ref: https://github.com/nlohmann/json
// Ref: https://github.com/Mbed-TLS/mbedtls

#include "patcher.hpp"

#include <mbedtls/sha256.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wemod::patcher
{
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
constexpr std::size_t block_size{4UZ * 1024UZ * 1024UZ};
constexpr std::size_t copy_chunk{1024UZ * 1024UZ};
constexpr std::string_view fuse_sentinel{
    "dL7pKGdnNz796PbbjQWNKmHXBZaB9tsX"};
constexpr std::size_t fuse_index{4UZ};
constexpr char fuse_off{'0'};
constexpr char fuse_on{'1'};
constexpr char fuse_removed{'r'};

struct temp_dir final
{
    fs::path path;
    explicit temp_dir(const std::string& base)
    {
        const auto stamp = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path = fs::temp_directory_path()
               / (base + std::to_string(stamp));
        fs::create_directories(path);
    }
    temp_dir(const temp_dir&) = delete;
    temp_dir& operator=(const temp_dir&) = delete;
    ~temp_dir()
    {
        std::error_code code;
        fs::remove_all(path, code);
    }
};

[[nodiscard]] std::uint32_t read_u32_le(const std::vector<std::uint8_t>& data,
                                        std::size_t pos)
{
    if (pos + 4 > data.size()) {
        throw std::runtime_error("unexpected end of buffer");
    }
    return static_cast<std::uint32_t>(data[pos])
           | (static_cast<std::uint32_t>(data[pos + 1]) << 8)
           | (static_cast<std::uint32_t>(data[pos + 2]) << 16)
           | (static_cast<std::uint32_t>(data[pos + 3]) << 24);
}

void append_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFU));
}

[[nodiscard]] std::vector<std::uint8_t> read_bytes(const fs::path& path)
{
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    std::string tmp{std::istreambuf_iterator<char>{in},
                    std::istreambuf_iterator<char>{}};
    return {tmp.begin(), tmp.end()};
}

[[nodiscard]] std::string read_text(const fs::path& path)
{
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    return {std::istreambuf_iterator<char>{in},
            std::istreambuf_iterator<char>{}};
}

void write_text(const fs::path& path, const std::string& text)
{
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    if (!out) {
        throw std::runtime_error("cannot write " + path.string());
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        throw std::runtime_error("failed writing " + path.string());
    }
}

[[nodiscard]] std::string hex_of(const std::array<std::uint8_t, 32>& sum)
{
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string out;
    out.reserve(64);
    for (const auto byte : sum) {
        out.push_back(digits[(byte >> 4) & 0xF]);
        out.push_back(digits[byte & 0xF]);
    }
    return out;
}

[[nodiscard]] std::array<std::uint8_t, 32>
sha256_once(const std::uint8_t* data, std::size_t size)
{
    std::array<std::uint8_t, 32> out{};
    if (mbedtls_sha256(data, size, out.data(), 0) != 0) {
        throw std::runtime_error("sha256 failed");
    }
    return out;
}

[[nodiscard]] json integrity_of(const fs::path& path)
{
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        throw std::runtime_error("cannot open " + path.string());
    }
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    if (mbedtls_sha256_starts_ret(&ctx, 0) != 0) {
        throw std::runtime_error("sha256 init failed");
    }
    std::vector<char> block(block_size);
    std::vector<std::string> blocks;
    while (in) {
        in.read(block.data(),
                static_cast<std::streamsize>(block.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (got == 0) {
            break;
        }
        if (mbedtls_sha256_update_ret(
                &ctx, reinterpret_cast<const unsigned char*>(block.data()),
                got)
            != 0) {
            mbedtls_sha256_free(&ctx);
            throw std::runtime_error("sha256 update failed");
        }
        blocks.push_back(hex_of(sha256_once(
            reinterpret_cast<const std::uint8_t*>(block.data()), got)));
    }
    std::array<std::uint8_t, 32> whole{};
    if (mbedtls_sha256_finish_ret(&ctx, whole.data()) != 0) {
        mbedtls_sha256_free(&ctx);
        throw std::runtime_error("sha256 finish failed");
    }
    mbedtls_sha256_free(&ctx);
    return json{{"algorithm", "SHA256"},
                {"hash", hex_of(whole)},
                {"blockSize", block_size},
                {"blocks", blocks}};
}

[[nodiscard]] std::vector<std::uint8_t>
pickle(const std::uint8_t* data, std::size_t size)
{
    std::vector<std::uint8_t> body;
    body.reserve(4 + size + 4);
    append_u32_le(body, static_cast<std::uint32_t>(size));
    body.insert(body.end(), data, data + size);
    while (body.size() % 4 != 0) {
        body.push_back(0);
    }
    std::vector<std::uint8_t> out;
    out.reserve(4 + body.size());
    append_u32_le(out, static_cast<std::uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

[[nodiscard]] std::vector<std::uint8_t>
size_pickle(std::uint32_t header_size)
{
    std::vector<std::uint8_t> out;
    append_u32_le(out, 4);
    append_u32_le(out, header_size);
    return out;
}

struct asar_view final
{
    json header;
    std::uint64_t base{0};
};

[[nodiscard]] asar_view read_asar(const fs::path& path)
{
    const auto bytes = read_bytes(path);
    if (bytes.size() < 8 || read_u32_le(bytes, 0) != 4) {
        throw std::runtime_error("invalid ASAR size pickle");
    }
    const auto header_size = read_u32_le(bytes, 4);
    if (bytes.size() < 8 + header_size || header_size < 8) {
        throw std::runtime_error("invalid ASAR header pickle");
    }
    const std::size_t off{8};
    const std::uint32_t payload = static_cast<std::uint32_t>(
        bytes[off] | (bytes[off + 1] << 8) | (bytes[off + 2] << 16)
        | (bytes[off + 3] << 24));
    const std::uint32_t json_size = static_cast<std::uint32_t>(
        bytes[off + 4] | (bytes[off + 5] << 8) | (bytes[off + 6] << 16)
        | (bytes[off + 7] << 24));
    if (payload + 4 != header_size || json_size > payload - 4) {
        throw std::runtime_error("invalid ASAR header sizes");
    }
    const std::string raw(reinterpret_cast<const char*>(bytes.data() + off
                                                        + 8),
                          json_size);
    asar_view view;
    view.header = json::parse(raw);
    view.base = 8 + header_size;
    return view;
}

[[nodiscard]] std::uint64_t json_u64(const json& value)
{
    if (value.is_string()) {
        return std::stoull(value.get<std::string>());
    }
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer()) {
        const auto got = value.get<std::int64_t>();
        if (got < 0) {
            throw std::runtime_error("negative ASAR size");
        }
        return static_cast<std::uint64_t>(got);
    }
    throw std::runtime_error("bad ASAR integer");
}

void collect_rels(const json& node, const std::string& prefix,
                  std::vector<std::string>& out)
{
    const auto files = node.find("files");
    if (files == node.end() || !files->is_object()) {
        return;
    }
    for (const auto& [name, item] : files->items()) {
        const std::string rel =
            prefix.empty() ? name : prefix + "/" + name;
        if (item.contains("files")) {
            collect_rels(item, rel, out);
        } else {
            out.push_back(rel);
        }
    }
}

[[nodiscard]] json& find_meta(json& header, const std::string& rel)
{
    json* node{&header};
    std::size_t begin{0};
    while (true) {
        const auto slash = rel.find('/', begin);
        const std::string part = (slash == std::string::npos)
                                     ? rel.substr(begin)
                                     : rel.substr(begin, slash - begin);
        node = &((*node)["files"][part]);
        if (slash == std::string::npos) {
            return *node;
        }
        begin = slash + 1;
    }
}

[[nodiscard]] const json& find_meta_const(const json& header,
                                          const std::string& rel)
{
    const json* node{&header};
    std::size_t begin{0};
    while (true) {
        const auto slash = rel.find('/', begin);
        const std::string part = (slash == std::string::npos)
                                     ? rel.substr(begin)
                                     : rel.substr(begin, slash - begin);
        node = &((*node).at("files").at(part));
        if (slash == std::string::npos) {
            return *node;
        }
        begin = slash + 1;
    }
}

[[nodiscard]] bool is_unpacked(const json& meta)
{
    const auto it = meta.find("unpacked");
    if (it == meta.end() || it->is_null()) {
        return false;
    }
    if (it->is_boolean()) {
        return it->get<bool>();
    }
    if (it->is_number()) {
        return it->get<int>() != 0;
    }
    return false;
}

[[nodiscard]] fs::path safe_join(const fs::path& root, const std::string& rel)
{
    if (rel.empty() || rel.front() == '/' || rel.front() == '\\') {
        throw std::runtime_error("unsafe ASAR path: " + rel);
    }
    fs::path out{root};
    std::size_t begin{0};
    bool any{false};
    while (begin <= rel.size()) {
        const auto slash = rel.find('/', begin);
        const std::string part = (slash == std::string::npos)
                                     ? rel.substr(begin)
                                     : rel.substr(begin, slash - begin);
        if (part.empty() || part == "." || part == "..") {
            throw std::runtime_error("unsafe ASAR path: " + rel);
        }
        if (part.find('\\') != std::string::npos) {
            throw std::runtime_error("unsafe ASAR path: " + rel);
        }
        out /= part;
        any = true;
        if (slash == std::string::npos) {
            break;
        }
        begin = slash + 1;
    }
    if (!any) {
        throw std::runtime_error("unsafe ASAR path: " + rel);
    }
    const auto root_text =
        fs::absolute(root).lexically_normal().generic_string();
    const auto out_text =
        fs::absolute(out).lexically_normal().generic_string();
    if (out_text != root_text
        && (out_text.size() <= root_text.size()
            || out_text.compare(0, root_text.size(), root_text) != 0
            || out_text[root_text.size()] != '/')) {
        throw std::runtime_error("unsafe ASAR path: " + rel);
    }
    return out;
}

[[nodiscard]] json extract_asar(const fs::path& asar, const fs::path& out)
{
    auto view = read_asar(asar);
    const fs::path unpacked{asar.string() + ".unpacked"};
    std::ifstream in{asar, std::ios::binary};
    if (!in) {
        throw std::runtime_error("cannot open " + asar.string());
    }
    std::vector<std::string> rels;
    collect_rels(view.header, "", rels);
    for (const auto& rel : rels) {
        const json& meta = find_meta_const(view.header, rel);
        if (meta.contains("link")) {
            continue;
        }
        const fs::path dst = safe_join(out, rel);
        fs::create_directories(dst.parent_path());
        if (is_unpacked(meta)) {
            const fs::path src = safe_join(unpacked, rel);
            if (!fs::is_regular_file(src)) {
                throw std::runtime_error("missing unpacked ASAR file: "
                                         + rel);
            }
            fs::copy_file(src, dst,
                          fs::copy_options::overwrite_existing);
            continue;
        }
        const auto offset = json_u64(meta.at("offset"));
        const auto size = json_u64(meta.at("size"));
        in.seekg(static_cast<std::streamoff>(view.base + offset),
                std::ios::beg);
        if (!in) {
            throw std::runtime_error("ASAR seek failed: " + rel);
        }
        std::ofstream dst_out{dst, std::ios::binary | std::ios::trunc};
        std::vector<char> buf(copy_chunk);
        std::uint64_t left{size};
        while (left > 0) {
            const std::size_t want =
                static_cast<std::size_t>(std::min<std::uint64_t>(
                    left, buf.size()));
            in.read(buf.data(),
                    static_cast<std::streamsize>(want));
            const auto got = static_cast<std::size_t>(in.gcount());
            if (got == 0) {
                throw std::runtime_error("truncated ASAR: " + rel);
            }
            dst_out.write(buf.data(),
                          static_cast<std::streamsize>(got));
            left -= got;
        }
    }
    return view.header;
}

void pack_asar(const fs::path& root, const fs::path& out, json& header)
{
    std::vector<fs::path> packed;
    std::uint64_t offset{0};
    std::vector<std::string> rels;
    collect_rels(header, "", rels);
    for (const auto& rel : rels) {
        json& meta = find_meta(header, rel);
        if (meta.contains("link")) {
            continue;
        }
        const fs::path src = safe_join(root, rel);
        if (!fs::is_regular_file(src)) {
            throw std::runtime_error("missing ASAR file: " + rel);
        }
        meta["size"] = fs::file_size(src);
        meta["integrity"] = integrity_of(src);
        if (is_unpacked(meta)) {
            meta.erase("offset");
            continue;
        }
        meta["offset"] = std::to_string(offset);
        packed.push_back(src);
        offset += json_u64(meta.at("size"));
    }
    const std::string raw = header.dump();
    const auto header_pickle = pickle(
        reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size());
    const fs::path tmp{out.string() + ".tmp"};
    {
        std::ofstream tmp_out{tmp, std::ios::binary | std::ios::trunc};
        if (!tmp_out) {
            throw std::runtime_error("cannot write " + tmp.string());
        }
        const auto head = size_pickle(
            static_cast<std::uint32_t>(header_pickle.size()));
        tmp_out.write(reinterpret_cast<const char*>(head.data()),
                      static_cast<std::streamsize>(head.size()));
        tmp_out.write(reinterpret_cast<const char*>(header_pickle.data()),
                      static_cast<std::streamsize>(header_pickle.size()));
        std::vector<char> buf(copy_chunk);
        for (const auto& src : packed) {
            std::ifstream src_in{src, std::ios::binary};
            while (src_in) {
                src_in.read(buf.data(),
                            static_cast<std::streamsize>(buf.size()));
                const auto got =
                    static_cast<std::streamsize>(src_in.gcount());
                if (got > 0) {
                    tmp_out.write(buf.data(), got);
                }
            }
        }
    }
    const auto check = read_asar(tmp);
    std::vector<std::string> check_rels;
    collect_rels(check.header, "", check_rels);
    std::uint64_t payload{0};
    for (const auto& rel : check_rels) {
        const json& meta = find_meta_const(check.header, rel);
        if (meta.contains("link") || is_unpacked(meta)) {
            continue;
        }
        payload += json_u64(meta.at("size"));
    }
    if (fs::file_size(tmp) != check.base + payload) {
        throw std::runtime_error(
            "ASAR verification failed: archive size mismatch");
    }
    std::error_code code;
    fs::remove(out, code);
    fs::rename(tmp, out, code);
    if (code) {
        throw std::filesystem::filesystem_error("rename tmp asar", tmp,
                                                out, code);
    }
}

[[nodiscard]] std::string strip_named_groups(const std::string& pattern)
{
    std::string out;
    out.reserve(pattern.size());
    for (std::size_t i{0}; i < pattern.size();) {
        if (i + 4 <= pattern.size() && pattern.compare(i, 4, "(?P<") == 0) {
            const auto end = pattern.find('>', i + 4);
            if (end == std::string::npos) {
                throw std::runtime_error("bad (?P<...> group");
            }
            out.push_back('(');
            i = end + 1;
        } else {
            out.push_back(pattern[i]);
            ++i;
        }
    }
    return out;
}

[[nodiscard]] std::string to_dotall(const std::string& pattern)
{
    std::string out;
    out.reserve(pattern.size() + 16);
    bool in_class{false};
    for (std::size_t i{0}; i < pattern.size(); ++i) {
        const char cur{pattern[i]};
        if (cur == '\\' && i + 1 < pattern.size()) {
            out.push_back(cur);
            out.push_back(pattern[++i]);
            continue;
        }
        if (cur == '[') {
            in_class = true;
        } else if (cur == ']') {
            in_class = false;
        }
        if (cur == '.' && !in_class) {
            out += "[\\s\\S]";
        } else {
            out.push_back(cur);
        }
    }
    return out;
}

struct bundle_patch final
{
    std::string name;
    std::regex pattern;
    std::function<std::string(const std::smatch&)> make;
};

[[nodiscard]] bundle_patch
make_patch(std::string name, const std::string& py_pattern,
           std::function<std::string(const std::smatch&)> make)
{
    const std::string converted =
        to_dotall(strip_named_groups(py_pattern));
    return {.name = std::move(name),
            .pattern = std::regex{converted,
                                  std::regex::ECMAScript
                                      | std::regex::optimize},
            .make = std::move(make)};
}

void apply_patches_in_text(std::string& text,
                           std::vector<bundle_patch>& pending,
                           const std::string& file_name)
{
    for (auto it = pending.begin(); it != pending.end();) {
        std::size_t count{0};
        std::smatch only;
        for (std::sregex_iterator cur{text.begin(), text.end(),
                                      it->pattern},
             end;
             cur != end; ++cur) {
            ++count;
            if (count == 1) {
                only = *cur;
            } else {
                break;
            }
        }
        if (count == 0) {
            ++it;
            continue;
        }
        if (count != 1) {
            throw std::runtime_error(it->name
                                     + ": expected one match, got many");
        }
        const std::string replacement = it->make(only);
        const auto pos = static_cast<std::size_t>(only.position(0));
        const auto len = static_cast<std::size_t>(only.length(0));
        text = text.substr(0, pos) + replacement + text.substr(pos + len);
        std::cout << "patched " << it->name << ": " << file_name << "\n";
        it = pending.erase(it);
    }
}

void patch_bundles(const fs::path& root)
{
    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        const bool is_index{name == "index.js"};
        const bool is_bundle = name.starts_with("app-")
                               && name.ends_with(".bundle.js");
        if (is_index || is_bundle) {
            candidates.push_back(entry.path());
        }
    }
    std::ranges::sort(candidates);
    std::vector<bundle_patch> pending;
    pending.push_back(make_patch(
        "activate-pro-account",
        R"js(getUserAccount\(\)\{.*?return\s+this\.#(?P<s>[\w$]+)\.fetch\(\{.*?\}\)\})js",
        [](const std::smatch& got) {
            const std::string slot{got[1].str()};
            return "getUserAccount(){return this.#" + slot
                   + ".fetch({endpoint:\"/v3/account\",method:"
                     "\"GET\",name:\"/v3/account\",collectMetrics:0})"
                     ".then(response=>{response.subscription={period:"
                     "\"yearly\",state:\"active\"};return response;})}";
        }));
    pending.push_back(make_patch(
        "activate-pro-language",
        R"js(setAccountLanguage\((?P<p>[^)]*)\)\{\s*return\s+(?P<e>this\.#[\w$]+\.post\("/v3/account/language",\{[^}]*\}\))\s*;?\s*\})js",
        [](const std::smatch& got) {
            return "setAccountLanguage(" + got[1].str() + "){return ("
                   + got[2].str()
                   + ").then(response=>{response&&\"object\"==typeof "
                     "response&&(response.subscription={period:\"yearly\""
                     ",state:\"active\"});return response;})}";
        }));
    pending.push_back(make_patch(
        "disable-native-pairing",
        R"js(requestRemoteAuthCode\(\)\{return this\.#[\w$]+\.post\("/v3/auth/remote_code"\)\})js",
        [](const std::smatch&) {
            return std::string{
                "requestRemoteAuthCode(){return Promise.reject(new Error("
                "\"wemod-enhancer: native mobile pairing disabled\"))}"};
        }));
    pending.push_back(make_patch(
        "disable-updates",
        R"js(registerHandler\("ACTION_CHECK_FOR_UPDATE".*?\)\)\)\))js",
        [](const std::smatch&) {
            return std::string{
                "registerHandler(\"ACTION_CHECK_FOR_UPDATE\","
                "(e=>expectUpdateFeedUrl(e,(e=>null)))"};
        }));
    pending.push_back(make_patch(
        "devtools-f12", R"js((?P<a>\w+)\.whenReady\(\)\.then\()js",
        [](const std::smatch& got) {
            const std::string app{got[1].str()};
            return app
                   + ".on(\"browser-window-created\",((_,w)=>{try{"
                     "w.webContents.on(\"before-input-event\",((_,i)=>{"
                     "if(\"F12\"===i.key&&\"keyDown\"===i.type){"
                     "w.webContents.isDevToolsOpened()?w.webContents."
                     "closeDevTools():w.webContents.openDevTools({mode:"
                     "\"detach\"})}}))}catch(e){}})),"
                   + app + ".whenReady().then(";
        }));
    std::vector<bundle_patch> optional;
    optional.push_back(make_patch(
        "activate-pro-brand",
        R"js(setAccountWandBrandExperience\(\)\{.*?return\s+this\.#(?P<s>[\w$]+)\.post\("/v3/account/brand_experience_wand"\)\})js",
        [](const std::smatch& got) {
            return "setAccountWandBrandExperience(){return this.#"
                   + got[1].str()
                   + ".post(\"/v3/account/brand_experience_wand\")"
                     ".then(response=>{response.subscription={period:"
                     "\"yearly\",state:\"active\"};return response;})}";
        }));
    for (const auto& path : candidates) {
        std::string text = read_text(path);
        const std::string file_name = path.filename().string();
        const std::size_t before_required{pending.size()};
        const std::size_t before_optional{optional.size()};
        apply_patches_in_text(text, pending, file_name);
        apply_patches_in_text(text, optional, file_name);
        if (pending.size() != before_required
            || optional.size() != before_optional) {
            write_text(path, text);
        }
    }
    for (const auto& item : optional) {
        std::cout << "skipped optional patch " << item.name
                  << ": endpoint not present in this client\n";
    }
    if (!pending.empty()) {
        std::string names;
        for (const auto& item : pending) {
            if (!names.empty()) {
                names += ", ";
            }
            names += item.name;
        }
        throw std::runtime_error("unsupported client; missing patches: "
                                 + names);
    }
}

[[nodiscard]] std::optional<std::size_t>
find_fuse_wire(const std::vector<std::uint8_t>& data)
{
    if (fuse_sentinel.size() != 32) {
        throw std::runtime_error("bad fuse sentinel size");
    }
    std::size_t start{0};
    const std::string needle{fuse_sentinel};
    while (start < data.size()) {
        const auto* begin =
            reinterpret_cast<const char*>(data.data() + start);
        const auto* end =
            reinterpret_cast<const char*>(data.data() + data.size());
        const auto* found =
            std::search(begin, end, needle.begin(), needle.end());
        if (found == end) {
            return std::nullopt;
        }
        const auto off = static_cast<std::size_t>(found - begin) + start;
        if (off + 34 <= data.size() && data[off + 32] == 1
            && data[off + 33] >= 5) {
            return off;
        }
        start = off + 1;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<char> fuse_state(const fs::path& exe)
{
    const auto data = read_bytes(exe);
    const auto off = find_fuse_wire(data);
    if (!off.has_value()) {
        return std::nullopt;
    }
    return static_cast<char>(data[*off + 34 + fuse_index]);
}

char set_fuse_state(const fs::path& exe, char state)
{
    if (state != fuse_off && state != fuse_on && state != fuse_removed) {
        throw std::runtime_error("invalid fuse state");
    }
    const auto off = find_fuse_wire(read_bytes(exe));
    if (!off.has_value()) {
        throw std::runtime_error("electron fuse wire not found in "
                                 + exe.string());
    }
    const std::size_t pos{*off + 34 + fuse_index};
    std::fstream file{exe, std::ios::in | std::ios::out | std::ios::binary};
    if (!file) {
        throw std::runtime_error("cannot open " + exe.string());
    }
    file.seekg(static_cast<std::streamoff>(pos), std::ios::beg);
    const int prev_int{file.get()};
    if (prev_int == EOF) {
        throw std::runtime_error("cannot read fuse in " + exe.string());
    }
    const char prev{static_cast<char>(prev_int)};
    if (prev != fuse_off && prev != fuse_on && prev != fuse_removed) {
        throw std::runtime_error("unexpected fuse value in "
                                 + exe.string());
    }
    if (prev == state) {
        return prev;
    }
    file.seekp(static_cast<std::streamoff>(pos), std::ios::beg);
    file.put(state);
    file.flush();
    if (!file) {
        throw std::runtime_error("cannot write fuse in " + exe.string());
    }
    return prev;
}

[[nodiscard]] std::optional<fs::path> electron_exe(const fs::path& install)
{
    for (const std::string_view name : {"Wand.exe", "WeMod.exe"}) {
        const fs::path candidate{install / name};
        if (fs::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

[[nodiscard]] fs::path exe_backup_path(const fs::path& exe)
{
    return fs::path{exe.string() + ".wemod-enhancer-backup"};
}

void cleanup_legacy_dll(const fs::path& install)
{
    const fs::path dll{install / "version.dll"};
    const fs::path dll_backup{install / "version.dll.wemod-enhancer-backup"};
    if (fs::is_regular_file(dll_backup)) {
        fs::copy_file(dll_backup, dll,
                      fs::copy_options::overwrite_existing);
        fs::remove(dll_backup);
        std::cout << "restored pre-proxy version.dll\n";
        return;
    }
    if (fs::is_regular_file(dll)) {
        std::cout << "kept unexpected version.dll - remove manually if it "
                     "is the retired proxy\n";
    }
}

void flip_fuse(const fs::path& exe)
{
    const fs::path backup{exe_backup_path(exe)};
    const auto prev = fuse_state(exe);
    if (!prev.has_value()) {
        std::cout << "skipped fuse flip: no electron fuse wire in "
                  << exe.filename().string() << "\n";
        return;
    }
    if (*prev == fuse_removed) {
        std::cout << "asar integrity fuse already removed in "
                  << exe.filename().string() << "\n";
        return;
    }
    fs::copy_file(exe, backup, fs::copy_options::overwrite_existing);
    set_fuse_state(exe, fuse_removed);
    std::cout << "removed asar integrity fuse in "
              << exe.filename().string() << " (was '" << *prev << "')\n";
}

void restore_fuse(const fs::path& exe)
{
    const fs::path backup{exe_backup_path(exe)};
    const fs::path legacy{exe.parent_path()
                          / "electron-fuse.wemod-enhancer-backup"};
    if (fs::is_regular_file(backup)) {
        fs::copy_file(backup, exe, fs::copy_options::overwrite_existing);
        fs::remove(backup);
        std::cout << "restored " << exe.filename().string()
                  << " from backup\n";
        return;
    }
    if (fs::is_regular_file(legacy)) {
        std::string prev = read_text(legacy);
        prev.erase(std::remove_if(prev.begin(), prev.end(),
                                  [](unsigned char cur) {
                                      return std::isspace(cur) != 0;
                                  }),
                   prev.end());
        if (!prev.empty() && fuse_state(exe) == fuse_removed) {
            set_fuse_state(exe, prev.front());
            std::cout << "restored asar integrity fuse in "
                      << exe.filename().string() << " (was '" << prev
                      << "')\n";
        }
        fs::remove(legacy);
    }
}

struct asar_paths final
{
    fs::path asar;
    fs::path backup;
};

[[nodiscard]] asar_paths paths(const fs::path& install)
{
    const fs::path resources{install / "resources"};
    return {.asar = resources / "app.asar",
            .backup = resources / "app.asar.backup"};
}
} // namespace

void patch(const fs::path& install_dir)
{
    const auto [asar, backup] = paths(install_dir);
    if (!fs::is_regular_file(asar)) {
        throw std::runtime_error("missing " + asar.string());
    }
    if (!fs::exists(backup)) {
        fs::copy_file(asar, backup);
    } else {
        fs::copy_file(backup, asar, fs::copy_options::overwrite_existing);
    }
    const auto exe = electron_exe(install_dir);
    bool exe_touched{false};
    try {
        const temp_dir work{"wemod-enhancer-"};
        json header = extract_asar(asar, work.path);
        patch_bundles(work.path);
        pack_asar(work.path, asar, header);
        cleanup_legacy_dll(install_dir);
        if (exe.has_value()) {
            flip_fuse(*exe);
            exe_touched = true;
        } else {
            std::cout << "skipped fuse flip: no Wand.exe/WeMod.exe next "
                         "to resources\n";
        }
        std::cout << "patched " << install_dir.string() << "\n";
    } catch (...) {
        fs::copy_file(backup, asar, fs::copy_options::overwrite_existing);
        if (exe.has_value() && exe_touched) {
            restore_fuse(*exe);
        }
        throw;
    }
}

void unpatch(const fs::path& install_dir)
{
    const auto [asar, backup] = paths(install_dir);
    if (!fs::exists(backup)) {
        throw std::runtime_error("missing " + backup.string());
    }
    fs::copy_file(backup, asar, fs::copy_options::overwrite_existing);
    cleanup_legacy_dll(install_dir);
    if (const auto exe = electron_exe(install_dir); exe.has_value()) {
        restore_fuse(*exe);
    }
    std::cout << "restored " << install_dir.string() << "\n";
}
} // namespace wemod::patcher
