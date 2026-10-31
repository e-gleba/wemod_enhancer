#include "patcher.hpp"

#include <ctre.hpp>
#include <mbedtls/sha256.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace wemod
{
namespace
{
namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr auto block_size = 4UZ * 1024UZ * 1024UZ;
constexpr auto fuse_sentinel = std::string_view("dL7pKGdnNz796PbbjQWNKmHXBZaB9tsX");
constexpr auto fuse_index = std::size_t(4);
constexpr auto fuse_off = char('0');
constexpr auto fuse_on = char('1');
constexpr auto fuse_removed = char('r');

constexpr std::uint32_t u32_le(std::string_view data, std::size_t pos) noexcept
{
    auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos]));
    auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos + 1]));
    auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos + 2]));
    auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[pos + 3]));
    return static_cast<std::uint32_t>(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24));
}

static std::string read_file(const fs::path& path)
{
    auto in = std::ifstream(path, std::ios::binary);
    if (!in) {
        throw std::system_error(errno, std::generic_category(), "open failed: " + path.string());
    }
    auto ec = std::error_code();
    auto size = fs::file_size(path, ec);
    if (ec) {
        throw fs::filesystem_error("size failed", path, ec);
    }
    auto data = std::string(static_cast<std::size_t>(size), '\0');
    if (!data.empty()) {
        in.read(data.data(), static_cast<std::streamsize>(data.size()));
        auto got = static_cast<std::size_t>(in.gcount());
        if (got != data.size()) {
            data.resize(got);
        }
    }
    if (in.bad()) {
        throw std::system_error(errno, std::generic_category(), "read failed: " + path.string());
    }
    return data;
}

static void write_file(const fs::path& path, std::string_view data)
{
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::system_error(errno, std::generic_category(), "open failed: " + path.string());
    }
    if (!data.empty()) {
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    if (!out) {
        throw std::system_error(errno, std::generic_category(), "write failed: " + path.string());
    }
    out.close();
    if (!out) {
        throw std::system_error(errno, std::generic_category(), "close failed: " + path.string());
    }
}

static void copy_checked(const fs::path& from, const fs::path& to)
{
    auto ec = std::error_code();
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        throw fs::filesystem_error("copy failed", from, to, ec);
    }
}

static void rename_checked(const fs::path& from, const fs::path& to)
{
    auto ec = std::error_code();
    fs::rename(from, to, ec);
    if (ec) {
        throw fs::filesystem_error("rename failed", from, to, ec);
    }
}

static fs::path safe_path(const fs::path& root, std::string_view rel)
{
    if (rel.empty() || rel[0] == '/') {
        throw std::runtime_error("unsafe ASAR path: " + std::string(rel));
    }
    auto out = fs::path(root);
    auto start = std::size_t(0);
    while (true) {
        auto pos = rel.find('/', start);
        auto token = pos == std::string_view::npos ? rel.substr(start) : rel.substr(start, pos - start);
        if (token.empty() || token == "." || token == "..") {
            throw std::runtime_error("unsafe ASAR path: " + std::string(rel));
        }
        out /= std::string(token);
        if (pos == std::string_view::npos) {
            break;
        }
        start = pos + 1;
    }
    auto ec1 = std::error_code();
    auto ec2 = std::error_code();
    auto canon_root = fs::weakly_canonical(root, ec1);
    auto canon_out = fs::weakly_canonical(out, ec2);
    if (ec1) {
        throw fs::filesystem_error("canonicalize failed", root, ec1);
    }
    if (ec2) {
        throw fs::filesystem_error("canonicalize failed", out, ec2);
    }
    auto ec3 = std::error_code();
    auto rel_check = fs::relative(canon_out, canon_root, ec3);
    if (ec3 || rel_check.empty() || rel_check.string().starts_with("..")) {
        throw std::runtime_error("unsafe ASAR path: " + std::string(rel));
    }
    return out;
}

struct entry
{
    std::string rel;
    json* meta;
};

static void collect(json& node, std::string_view prefix, std::vector<entry>& out)
{
    auto it = node.find("files");
    if (it == node.end() || !it->is_object()) {
        return;
    }
    for (auto& [name, item] : it->items()) {
        auto rel = prefix.empty() ? std::string(name) : std::string(prefix) + "/" + name;
        if (item.contains("files")) {
            collect(item, rel, out);
        } else {
            out.emplace_back(rel, &item);
        }
    }
}

static std::uint64_t as_u64(const json& value)
{
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer()) {
        return static_cast<std::uint64_t>(value.get<std::int64_t>());
    }
    return static_cast<std::uint64_t>(std::stoull(value.get<std::string>()));
}

static std::pair<json, std::uint64_t> read_asar(const fs::path& path)
{
    auto in = std::ifstream(path, std::ios::binary);
    if (!in) {
        throw std::system_error(errno, std::generic_category(), "open failed: " + path.string());
    }
    auto head = std::array<char, 8>();
    in.read(head.data(), 8);
    if (in.gcount() != 8) {
        throw std::runtime_error("invalid ASAR size pickle");
    }
    auto view = std::string_view(head.data(), 8);
    if (u32_le(view, 0) != 4) {
        throw std::runtime_error("invalid ASAR size pickle");
    }
    auto header_size = u32_le(view, 4);
    auto raw = std::string(header_size, '\0');
    in.read(raw.data(), static_cast<std::streamsize>(header_size));
    if (static_cast<std::size_t>(in.gcount()) != header_size || header_size < 8) {
        throw std::runtime_error("invalid ASAR header pickle");
    }
    auto raw_view = std::string_view(raw.data(), raw.size());
    auto payload_size = u32_le(raw_view, 0);
    auto json_size = u32_le(raw_view, 4);
    if (payload_size + 4 != header_size || json_size > payload_size - 4) {
        throw std::runtime_error("invalid ASAR header sizes");
    }
    auto header = json::parse(raw.substr(8, json_size));
    return std::pair<json, std::uint64_t>(std::move(header), 8ULL + header_size);
}

struct sha_ctx
{
    mbedtls_sha256_context ctx;
    sha_ctx()
    {
        mbedtls_sha256_init(&ctx);
    }
    ~sha_ctx()
    {
        mbedtls_sha256_free(&ctx);
    }
    sha_ctx(const sha_ctx&) = delete;
    sha_ctx& operator=(const sha_ctx&) = delete;
};

static std::string hex_of(std::span<const unsigned char> data)
{
    constexpr auto digits = std::string_view("0123456789abcdef");
    auto hex = std::string();
    hex.reserve(data.size() * 2);
    for (auto byte : data) {
        hex += digits[byte >> 4];
        hex += digits[byte & 0xF];
    }
    return hex;
}

static std::string sha_hex(std::span<const unsigned char> data)
{
    auto out = std::array<unsigned char, 32>();
    auto ret = mbedtls_sha256(data.data(), data.size(), out.data(), 0);
    if (ret != 0) {
        throw std::runtime_error(std::format("sha256 failed: {}", ret));
    }
    return hex_of(std::span<const unsigned char>(out.data(), out.size()));
}

static json file_integrity(const fs::path& path)
{
    auto in = std::ifstream(path, std::ios::binary);
    if (!in) {
        throw std::system_error(errno, std::generic_category(), "open failed: " + path.string());
    }
    auto whole = sha_ctx();
    auto started = mbedtls_sha256_starts(&whole.ctx, 0);
    if (started != 0) {
        throw std::runtime_error(std::format("sha256 failed: {}", started));
    }
    auto buf = std::vector<unsigned char>(block_size);
    auto blocks = json::array();
    while (true) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        auto count = static_cast<std::size_t>(in.gcount());
        if (count == 0) {
            break;
        }
        auto chunk = std::span<const unsigned char>(buf.data(), count);
        auto updated = mbedtls_sha256_update(&whole.ctx, chunk.data(), chunk.size());
        if (updated != 0) {
            throw std::runtime_error(std::format("sha256 failed: {}", updated));
        }
        blocks.push_back(sha_hex(chunk));
        if (in.eof()) {
            break;
        }
        if (!in) {
            throw std::system_error(errno, std::generic_category(), "read failed: " + path.string());
        }
    }
    if (in.bad()) {
        throw std::system_error(errno, std::generic_category(), "read failed: " + path.string());
    }
    auto tail = std::array<unsigned char, 32>();
    auto finished = mbedtls_sha256_finish(&whole.ctx, tail.data());
    if (finished != 0) {
        throw std::runtime_error(std::format("sha256 failed: {}", finished));
    }
    auto out = json();
    out["algorithm"] = "SHA256";
    out["hash"] = hex_of(std::span<const unsigned char>(tail.data(), tail.size()));
    out["blockSize"] = block_size;
    out["blocks"] = std::move(blocks);
    return out;
}

static std::string pickle_blob(std::string_view payload)
{
    auto body_len = static_cast<std::uint32_t>(payload.size()) + 4;
    auto pad = static_cast<std::size_t>((4 - body_len % 4) % 4);
    auto body = std::string(body_len + pad, '\0');
    body[0] = static_cast<char>(body_len & 0xFF);
    body[1] = static_cast<char>((body_len >> 8) & 0xFF);
    body[2] = static_cast<char>((body_len >> 16) & 0xFF);
    body[3] = static_cast<char>((body_len >> 24) & 0xFF);
    body.replace(4, payload.size(), payload.data(), payload.size());
    auto total = static_cast<std::uint32_t>(body.size());
    auto out = std::string(4 + body.size(), '\0');
    out[0] = static_cast<char>(total & 0xFF);
    out[1] = static_cast<char>((total >> 8) & 0xFF);
    out[2] = static_cast<char>((total >> 16) & 0xFF);
    out[3] = static_cast<char>((total >> 24) & 0xFF);
    out.replace(4, body.size(), body.data(), body.size());
    return out;
}

static std::string size_pickle(std::uint32_t value)
{
    auto out = std::string(8, '\0');
    out[0] = 4;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    out[4] = static_cast<char>(value & 0xFF);
    out[5] = static_cast<char>((value >> 8) & 0xFF);
    out[6] = static_cast<char>((value >> 16) & 0xFF);
    out[7] = static_cast<char>((value >> 24) & 0xFF);
    return out;
}

static json extract_asar(const fs::path& asar, const fs::path& out_dir)
{
    auto parsed = read_asar(asar);
    auto header = std::move(parsed.first);
    auto base = parsed.second;
    auto unpacked = fs::path(asar.string() + ".unpacked");
    auto in = std::ifstream(asar, std::ios::binary);
    if (!in) {
        throw std::system_error(errno, std::generic_category(), "open failed: " + asar.string());
    }
    auto ents = std::vector<entry>();
    collect(header, "", ents);
    for (auto& ent : ents) {
        if (ent.meta->contains("link")) {
            continue;
        }
        auto dst = safe_path(out_dir, ent.rel);
        auto ec = std::error_code();
        fs::create_directories(dst.parent_path(), ec);
        if (ec) {
            throw fs::filesystem_error("mkdir failed", dst.parent_path(), ec);
        }
        if (ent.meta->value("unpacked", false)) {
            auto src = safe_path(unpacked, ent.rel);
            auto ec2 = std::error_code();
            if (!fs::is_regular_file(src, ec2) || ec2) {
                throw std::runtime_error("missing unpacked ASAR file: " + ent.rel);
            }
            copy_checked(src, dst);
            continue;
        }
        auto off = as_u64(ent.meta->at("offset"));
        auto size = as_u64(ent.meta->at("size"));
        in.seekg(static_cast<std::streamoff>(base + off), std::ios::beg);
        if (!in) {
            throw std::system_error(errno, std::generic_category(), "seek failed: " + asar.string());
        }
        auto blob = std::string(size, '\0');
        if (size > 0) {
            in.read(blob.data(), static_cast<std::streamsize>(size));
            if (static_cast<std::size_t>(in.gcount()) != size) {
                throw std::runtime_error("truncated ASAR payload: " + ent.rel);
            }
        }
        write_file(dst, blob);
    }
    return header;
}

static void pack_asar(const fs::path& root, const fs::path& out, json& header)
{
    auto ents = std::vector<entry>();
    collect(header, "", ents);
    auto packed = std::vector<fs::path>();
    auto offset = std::uint64_t(0);
    for (auto& ent : ents) {
        if (ent.meta->contains("link")) {
            continue;
        }
        auto src = safe_path(root, ent.rel);
        auto ec = std::error_code();
        if (!fs::is_regular_file(src, ec) || ec) {
            throw std::runtime_error("missing ASAR file: " + ent.rel);
        }
        auto size = fs::file_size(src, ec);
        if (ec) {
            throw fs::filesystem_error("size failed", src, ec);
        }
        (*ent.meta)["size"] = size;
        (*ent.meta)["integrity"] = file_integrity(src);
        if (ent.meta->value("unpacked", false)) {
            ent.meta->erase("offset");
            continue;
        }
        (*ent.meta)["offset"] = std::to_string(offset);
        packed.push_back(src);
        offset += size;
    }
    auto raw = header.dump();
    auto pickled = pickle_blob(raw);
    auto staged = fs::path(out.string() + ".tmp");
    auto ec = std::error_code();
    fs::remove(staged, ec);
    if (ec) {
        throw fs::filesystem_error("remove stale tmp failed", staged, ec);
    }
    auto tmp = std::ofstream(staged, std::ios::binary | std::ios::trunc);
    if (!tmp) {
        throw std::system_error(errno, std::generic_category(), "open failed: " + staged.string());
    }
    auto prefix = size_pickle(static_cast<std::uint32_t>(pickled.size()));
    tmp.write(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    tmp.write(pickled.data(), static_cast<std::streamsize>(pickled.size()));
    if (!tmp) {
        throw std::system_error(errno, std::generic_category(), "write failed: " + staged.string());
    }
    auto chunk = std::vector<char>(1024UZ * 1024UZ);
    for (auto& src : packed) {
        auto in = std::ifstream(src, std::ios::binary);
        if (!in) {
            throw std::system_error(errno, std::generic_category(), "open failed: " + src.string());
        }
        while (in) {
            in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            auto count = static_cast<std::size_t>(in.gcount());
            if (count == 0) {
                break;
            }
            tmp.write(chunk.data(), static_cast<std::streamsize>(count));
            if (!tmp) {
                throw std::system_error(errno, std::generic_category(), "write failed: " + staged.string());
            }
        }
        if (in.bad()) {
            throw std::system_error(errno, std::generic_category(), "read failed: " + src.string());
        }
    }
    tmp.close();
    if (!tmp) {
        throw std::system_error(errno, std::generic_category(), "close failed: " + staged.string());
    }
    auto check = read_asar(staged);
    auto data_off = check.second;
    auto cent = std::vector<entry>();
    collect(check.first, "", cent);
    auto total = data_off;
    for (auto& ent : cent) {
        if (ent.meta->contains("link") || ent.meta->value("unpacked", false)) {
            continue;
        }
        total += as_u64(ent.meta->at("size"));
    }
    auto ec2 = std::error_code();
    auto actual = fs::file_size(staged, ec2);
    if (ec2) {
        throw fs::filesystem_error("size failed", staged, ec2);
    }
    if (actual != total) {
        throw std::runtime_error("ASAR verification failed: archive size mismatch");
    }
    rename_checked(staged, out);
}

template <ctll::fixed_string Pat>
static bool replace_once(std::string& text, std::string_view name, auto&& make)
{
    auto view = std::string_view(text.data(), text.size());
    auto first = ctre::search<Pat>(view);
    if (!first) {
        return false;
    }
    auto whole = first.template get<0>().to_view();
    auto off = static_cast<std::size_t>(whole.data() - text.data());
    auto after_off = off + whole.size();
    auto after = std::string_view(text.data() + after_off, text.size() - after_off);
    auto extra = std::size_t(0);
    auto rest = after;
    while (true) {
        auto next = ctre::search<Pat>(rest);
        if (!next) {
            break;
        }
        extra += 1;
        auto part = next.template get<0>().to_view();
        auto at = static_cast<std::size_t>(part.data() - rest.data());
        auto adv = at + part.size();
        if (adv >= rest.size()) {
            break;
        }
        rest = std::string_view(rest.data() + adv, rest.size() - adv);
    }
    if (extra > 0) {
        throw std::runtime_error(std::format("{}: expected one match, got {}", name, 1 + extra));
    }
    auto rep = make(first);
    auto out = std::string();
    out.reserve(text.size() + rep.size());
    out.append(text.data(), off);
    out += rep;
    out.append(text.data() + after_off, text.size() - after_off);
    text = std::move(out);
    return true;
}

static bool apply_account(std::string& text)
{
    return replace_once<R"regex(ACCOUNT)regex">(text, "activate-pro-account", [](auto& m) {
        auto s = m.template get<1>().to_view();
        auto out = std::string("getUserAccount(){return this.#");
        out.append(s.data(), s.size());
        out += std::string(R"js(.fetch({endpoint:"/v3/account",method:"GET",name:"/v3/account",collectMetrics:0}).then(response=>{response.subscription={period:"yearly",state:"active"};return response;})}js");
        return out;
    });
}

static bool apply_language(std::string& text)
{
    return replace_once<R"regex(LANGUAGE)regex">(text, "activate-pro-language", [](auto& m) {
        auto p = m.template get<1>().to_view();
        auto e = m.template get<2>().to_view();
        auto out = std::string("setAccountLanguage(");
        out.append(p.data(), p.size());
        out += std::string("){return (");
        out.append(e.data(), e.size());
        out += std::string(R"js().then(response=>{response&&"object"==typeof response&&(response.subscription={period:"yearly",state:"active"});return response;})}js");
        return out;
    });
}

static bool apply_pairing(std::string& text)
{
    return replace_once<R"regex(PAIRING)regex">(text, "disable-native-pairing", [](auto&) {
        return std::string(R"js(requestRemoteAuthCode(){return Promise.reject(new Error("wemod-enhancer: native mobile pairing disabled"))})js");
    });
}

static bool apply_updates(std::string& text)
{
    return replace_once<R"regex(UPDATES)regex">(text, "disable-updates", [](auto&) {
        return std::string(R"js(registerHandler("ACTION_CHECK_FOR_UPDATE",(e=>expectUpdateFeedUrl(e,(e=>null)))js");
    });
}

static bool apply_devtools(std::string& text)
{
    return replace_once<R"regex(DEVTOOLS)regex">(text, "devtools-f12", [](auto& m) {
        auto a = m.template get<1>().to_view();
        auto out = std::string(a.data(), a.size());
        out += std::string(R"js(.on("browser-window-created",((_,w)=>{try{w.webContents.on("before-input-event",((_,i)=>{if("F12"===i.key&&"keyDown"===i.type){w.webContents.isDevToolsOpened()?w.webContents.closeDevTools():w.webContents.openDevTools({mode:"detach"})}}))}catch(e){}})),)js");
        out.append(a.data(), a.size());
        out += std::string(".whenReady().then(");
        return out;
    });
}

static bool apply_brand(std::string& text)
{
    return replace_once<R"regex(BRAND)regex">(text, "activate-pro-brand", [](auto& m) {
        auto s = m.template get<1>().to_view();
        auto out = std::string("setAccountWandBrandExperience(){return this.#");
        out.append(s.data(), s.size());
        out += std::string(R"js(.post("/v3/account/brand_experience_wand").then(response=>{response.subscription={period:"yearly",state:"active"};return response;})}js");
        return out;
    });
}

static bool apply_named(std::string_view name, std::string& text)
{
    if (name == "activate-pro-account") {
        return apply_account(text);
    }
    if (name == "activate-pro-language") {
        return apply_language(text);
    }
    if (name == "disable-native-pairing") {
        return apply_pairing(text);
    }
    if (name == "disable-updates") {
        return apply_updates(text);
    }
    if (name == "devtools-f12") {
        return apply_devtools(text);
    }
    if (name == "activate-pro-brand") {
        return apply_brand(text);
    }
    throw std::runtime_error("unknown patch: " + std::string(name));
}

struct todo
{
    std::string_view name;
    bool done;
};

static void patch_bundles(const fs::path& root)
{
    auto ec = std::error_code();
    auto cands = std::vector<fs::path>();
    auto iter = fs::directory_iterator(root, ec);
    if (ec) {
        throw fs::filesystem_error("list failed", root, ec);
    }
    auto stop = fs::directory_iterator();
    for (; iter != stop; iter.increment(ec)) {
        if (ec) {
            throw fs::filesystem_error("list failed", root, ec);
        }
        auto ec2 = std::error_code();
        auto regular = iter->is_regular_file(ec2);
        if (ec2) {
            throw fs::filesystem_error("stat failed", iter->path(), ec2);
        }
        if (!regular) {
            continue;
        }
        auto name = iter->path().filename().string();
        if (name == "index.js" || (name.starts_with("app-") && name.ends_with(".bundle.js"))) {
            cands.push_back(iter->path());
        }
    }
    auto pend = std::vector<todo>();
    pend.emplace_back("activate-pro-account", false);
    pend.emplace_back("activate-pro-language", false);
    pend.emplace_back("disable-native-pairing", false);
    pend.emplace_back("disable-updates", false);
    pend.emplace_back("devtools-f12", false);
    auto opt = std::vector<todo>();
    opt.emplace_back("activate-pro-brand", false);
    for (auto& path : cands) {
        auto text = read_file(path);
        auto changed = false;
        for (auto& item : pend) {
            if (item.done) {
                continue;
            }
            if (apply_named(item.name, text)) {
                item.done = true;
                changed = true;
                std::print(stdout, "patched {}: {}\n", item.name, path.filename().string());
            }
        }
        for (auto& item : opt) {
            if (item.done) {
                continue;
            }
            if (apply_named(item.name, text)) {
                item.done = true;
                changed = true;
                std::print(stdout, "patched {}: {}\n", item.name, path.filename().string());
            }
        }
        if (changed) {
            write_file(path, text);
        }
    }
    for (auto& item : opt) {
        if (!item.done) {
            std::print(stdout, "skipped optional patch {}: endpoint not present in this client\n", item.name);
        }
    }
    auto missing = std::string();
    for (auto name : pend | std::views::filter([](const todo& item) { return !item.done; }) | std::views::transform(&todo::name)) {
        if (!missing.empty()) {
            missing += ", ";
        }
        missing.append(name.data(), name.size());
    }
    if (!missing.empty()) {
        throw std::runtime_error("unsupported client; missing patches: " + missing);
    }
}

static std::optional<std::size_t> find_fuse_wire(std::string_view data)
{
    auto start = std::size_t(0);
    while (start < data.size()) {
        auto pos = data.find(fuse_sentinel, start);
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        if (pos + 34 <= data.size() && static_cast<unsigned char>(data[pos + 32]) == 1 && static_cast<unsigned char>(data[pos + 33]) >= 5) {
            return pos;
        }
        start = pos + 1;
    }
    return std::nullopt;
}

static std::optional<char> fuse_state(const fs::path& exe)
{
    auto data = read_file(exe);
    auto view = std::string_view(data.data(), data.size());
    auto off = find_fuse_wire(view);
    if (!off) {
        return std::nullopt;
    }
    return data[*off + 34 + fuse_index];
}

static char set_fuse_state(const fs::path& exe, char state)
{
    if (state != fuse_off && state != fuse_on && state != fuse_removed) {
        throw std::runtime_error(std::format("invalid fuse state '{}'", state));
    }
    auto data = read_file(exe);
    auto view = std::string_view(data.data(), data.size());
    auto off = find_fuse_wire(view);
    if (!off) {
        throw std::runtime_error("electron fuse wire not found in " + exe.string());
    }
    auto pos = *off + 34 + fuse_index;
    auto prev = data[pos];
    if (prev != fuse_off && prev != fuse_on && prev != fuse_removed) {
        throw std::runtime_error(std::format("unexpected fuse value '{}' in {}", prev, exe.string()));
    }
    if (prev == state) {
        return prev;
    }
    data[pos] = state;
    write_file(exe, data);
    return prev;
}

static std::optional<fs::path> electron_exe(const fs::path& install)
{
    auto names = std::array<std::string_view, 2>({"Wand.exe", "WeMod.exe"});
    for (auto name : names) {
        auto cand = install / std::string(name);
        auto ec = std::error_code();
        if (fs::is_regular_file(cand, ec) && !ec) {
            return cand;
        }
        if (ec) {
            throw fs::filesystem_error("stat failed", cand, ec);
        }
    }
    return std::nullopt;
}

static fs::path exe_backup_path(const fs::path& exe)
{
    return fs::path(exe.string() + ".wemod-enhancer-backup");
}

static void cleanup_legacy_dll(const fs::path& install)
{
    auto dll = install / "version.dll";
    auto saved = install / "version.dll.wemod-enhancer-backup";
    auto ec = std::error_code();
    if (fs::is_regular_file(saved, ec) && !ec) {
        copy_checked(saved, dll);
        fs::remove(saved, ec);
        if (ec) {
            throw fs::filesystem_error("remove failed", saved, ec);
        }
        std::print(stdout, "restored pre-proxy {}\n", dll.filename().string());
        return;
    }
    if (ec) {
        throw fs::filesystem_error("stat failed", saved, ec);
    }
    if (fs::is_regular_file(dll, ec) && !ec) {
        std::print(stdout, "kept unexpected {} - remove manually if it is the retired proxy\n", dll.filename().string());
        return;
    }
    if (ec) {
        throw fs::filesystem_error("stat failed", dll, ec);
    }
}

static void flip_fuse(const fs::path& exe)
{
    auto backup = exe_backup_path(exe);
    auto prev = fuse_state(exe);
    if (!prev) {
        std::print(stdout, "skipped fuse flip: no electron fuse wire in {}\n", exe.filename().string());
        return;
    }
    if (*prev == fuse_removed) {
        std::print(stdout, "asar integrity fuse already removed in {}\n", exe.filename().string());
        return;
    }
    copy_checked(exe, backup);
    auto staged = fs::path(exe.string() + ".wemod-enhancer-tmp");
    auto ec = std::error_code();
    fs::remove(staged, ec);
    if (ec) {
        throw fs::filesystem_error("remove stale tmp failed", staged, ec);
    }
    copy_checked(exe, staged);
    set_fuse_state(staged, fuse_removed);
    rename_checked(staged, exe);
    std::print(stdout, "removed asar integrity fuse in {} (was '{}')\n", exe.filename().string(), *prev);
}

static void restore_fuse(const fs::path& exe)
{
    auto backup = exe_backup_path(exe);
    auto legacy = exe.parent_path() / "electron-fuse.wemod-enhancer-backup";
    auto ec = std::error_code();
    if (fs::is_regular_file(backup, ec) && !ec) {
        auto staged = fs::path(exe.string() + ".wemod-enhancer-tmp");
        fs::remove(staged, ec);
        if (ec) {
            throw fs::filesystem_error("remove stale tmp failed", staged, ec);
        }
        copy_checked(backup, staged);
        rename_checked(staged, exe);
        fs::remove(backup, ec);
        if (ec) {
            throw fs::filesystem_error("remove failed", backup, ec);
        }
        std::print(stdout, "restored {} from backup\n", exe.filename().string());
        return;
    }
    if (ec) {
        throw fs::filesystem_error("stat failed", backup, ec);
    }
    if (fs::is_regular_file(legacy, ec) && !ec) {
        auto text = read_file(legacy);
        auto start = text.find_first_not_of(" \t\r\n");
        auto stop = text.find_last_not_of(" \t\r\n");
        auto prev = start == std::string::npos ? std::string() : text.substr(start, stop - start + 1);
        auto cur = fuse_state(exe);
        if (!prev.empty() && cur && *cur == fuse_removed) {
            auto staged = fs::path(exe.string() + ".wemod-enhancer-tmp");
            fs::remove(staged, ec);
            if (ec) {
                throw fs::filesystem_error("remove stale tmp failed", staged, ec);
            }
            copy_checked(exe, staged);
            set_fuse_state(staged, prev[0]);
            rename_checked(staged, exe);
            std::print(stdout, "restored asar integrity fuse in {} (was '{}')\n", exe.filename().string(), prev);
        }
        fs::remove(legacy, ec);
        if (ec) {
            throw fs::filesystem_error("remove failed", legacy, ec);
        }
    } else if (ec) {
        throw fs::filesystem_error("stat failed", legacy, ec);
    }
}

} // namespace

void patch(const fs::path& install)
{
    auto asar = install / "resources" / "app.asar";
    auto backup = install / "resources" / "app.asar.backup";
    auto ec = std::error_code();
    if (!fs::is_regular_file(asar, ec) || ec) {
        throw std::runtime_error("missing " + asar.string());
    }
    if (!fs::exists(backup, ec) || ec) {
        if (ec) {
            throw fs::filesystem_error("stat failed", backup, ec);
        }
        copy_checked(asar, backup);
    } else {
        copy_checked(backup, asar);
    }
    auto exe = electron_exe(install);
    auto touched = false;
    try {
        auto work = asar.parent_path() / ".wemod-enhancer-work-tmp";
        auto stale = fs::path(asar.string() + ".tmp");
        fs::remove_all(work, ec);
        if (ec) {
            throw fs::filesystem_error("remove stale work failed", work, ec);
        }
        fs::remove(stale, ec);
        if (ec) {
            throw fs::filesystem_error("remove stale tmp failed", stale, ec);
        }
        fs::create_directories(work, ec);
        if (ec) {
            throw fs::filesystem_error("mkdir failed", work, ec);
        }
        auto header = extract_asar(asar, work);
        patch_bundles(work);
        pack_asar(work, asar, header);
        fs::remove_all(work, ec);
        if (ec) {
            throw fs::filesystem_error("cleanup work failed", work, ec);
        }
        cleanup_legacy_dll(install);
        if (exe) {
            flip_fuse(*exe);
            touched = true;
        } else {
            std::print(stdout, "skipped fuse flip: no Wand.exe/WeMod.exe next to resources\n");
        }
        std::print(stdout, "patched {}\n", install.string());
    } catch (...) {
        copy_checked(backup, asar);
        if (exe && touched) {
            restore_fuse(*exe);
        }
        throw;
    }
}

void unpatch(const fs::path& install)
{
    auto asar = install / "resources" / "app.asar";
    auto backup = install / "resources" / "app.asar.backup";
    auto ec = std::error_code();
    if (!fs::exists(backup, ec) || ec) {
        throw std::runtime_error("missing " + backup.string());
    }
    auto staged = fs::path(asar.string() + ".wemod-enhancer-tmp");
    fs::remove(staged, ec);
    if (ec) {
        throw fs::filesystem_error("remove stale tmp failed", staged, ec);
    }
    copy_checked(backup, staged);
    rename_checked(staged, asar);
    cleanup_legacy_dll(install);
    if (auto exe = electron_exe(install)) {
        restore_fuse(*exe);
    }
    std::print(stdout, "restored {}\n", install.string());
}

} // namespace wemod
