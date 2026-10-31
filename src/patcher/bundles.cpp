#include "bundles.hpp"

#include "asar.hpp"

#include <algorithm>
#include <ranges>
#include <regex>

namespace wemod
{
namespace
{
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
const std::vector<patch_spec>& patch_specs()
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

std::string apply_patch(const patch_spec& spec, const std::smatch& match)
{
    // Replacement text matches the Python lambdas exactly, including
    // capture substitution.
    if (!spec.needs_format) {
        return spec.literal;
    }
    if (spec.name == "activate-pro-account") {
        // Python f-string {{/}} escapes mean single braces here.
        const std::string part{match[1UZ].str()};
        return "getUserAccount(){return this.#" + part +
               ".fetch({endpoint:\"/v3/account\",method:\"GET\",name:\"/v3/account\",collectMetrics:0}).then("
               "response=>{response.subscription={period:\"yearly\",state:\"active\"};return response;})}";
    }
    if (spec.name == "activate-pro-language") {
        const std::string param{match[1UZ].str()};
        const std::string expr{match[2UZ].str()};
        return "setAccountLanguage(" + param + "){return (" + expr +
               ").then(response=>{response&&\"object\"==typeof response&&(response.subscription={period:\"yearly\","
               "state:\"active\"});return response;})}";
    }
    if (spec.name == "devtools-f12") {
        const std::string app{match[1UZ].str()};
        return app + ".on(\"browser-window-created\",((_,w)=>{try{w.webContents.on(\"before-input-event\",((_,i)=>{if("
                      "\"F12\"===i.key&&\"keyDown\"===i.type){w.webContents.isDevToolsOpened()?w.webContents."
                      "closeDevTools():w.webContents.openDevTools({mode:\"detach\"})}}))}catch(e){}}))," +
               app + ".whenReady().then(";
    }
    if (spec.name == "activate-pro-brand") {
        const std::string part{match[1UZ].str()};
        return "setAccountWandBrandExperience(){return this.#" + part +
               ".post(\"/v3/account/brand_experience_wand\").then(response=>{response.subscription={period:\"yearly\","
               "state:\"active\"};return response;})}";
    }
    return {};
}

std::size_t count_matches(const std::regex& expr, const std::string& text)
{
    const std::sregex_iterator begin{text.begin(), text.end(), expr};
    return static_cast<std::size_t>(std::distance(begin, std::sregex_iterator{}));
}

// One patch job: spec plus its compiled pattern, erased together on success.
struct pending_patch final
{
    patch_spec spec;
    std::regex expr;
};

// Apply every job whose pattern hits this file, erasing each job once its
// single required match is replaced. Unmatched jobs stay for later files.
std::expected<std::monostate, std::string> apply_all(std::vector<pending_patch>& jobs, std::string& text,
                                                     const fs::path& path, line_sink& out)
{
    for (std::size_t at{0UZ}; at < jobs.size();) {
        const std::size_t found{count_matches(jobs[at].expr, text)};
        if (found == 0UZ) {
            ++at;
            continue;
        }
        std::smatch match;
        if (found != 1UZ || !std::regex_search(text, match, jobs[at].expr)) {
            return std::unexpected<std::string>{jobs[at].spec.name + ": expected one match, got " +
                                                std::to_string(found)};
        }
        text = std::regex_replace(text, jobs[at].expr, apply_patch(jobs[at].spec, match),
                                  std::regex_constants::format_first_only);
        out.emit("patched " + jobs[at].spec.name + ": " + path.filename().string());
        jobs.erase(jobs.begin() + static_cast<std::ptrdiff_t>(at));
    }
    return std::monostate{};
}

bool is_patchable(const fs::directory_entry& entry)
{
    std::error_code probe;
    if (!entry.is_regular_file(probe) || probe) {
        return false;
    }
    const std::string name{entry.path().filename().string()};
    const bool bundle{name.size() > 12UZ && name.starts_with("app-") && name.ends_with(".bundle.js")};
    return name == "index.js" || bundle;
}
} // namespace

std::expected<std::monostate, std::string> patch_bundles(const fs::path& root, line_sink& out)
{
    std::vector<fs::path> candidates;
    std::error_code error;
    std::ranges::copy_if(fs::directory_iterator{root, error}, fs::directory_iterator{}, std::back_inserter(candidates),
                         is_patchable);
    if (error) {
        return std::unexpected<std::string>{"cannot list " + root.string()};
    }
    std::ranges::sort(candidates);
    std::vector<pending_patch> pending;
    std::vector<pending_patch> optional;
    try {
        for (const patch_spec& spec : patch_specs()) {
            pending_patch job{.spec = spec, .expr = std::regex{spec.pattern, std::regex::ECMAScript}};
            if (spec.required) {
                pending.push_back(std::move(job));
                continue;
            }
            optional.push_back(std::move(job));
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
        const std::string before{text};
        auto patched{apply_all(pending, text, path, out)};
        if (!patched) {
            return std::unexpected<std::string>{patched.error()};
        }
        auto bonus{apply_all(optional, text, path, out)};
        if (!bonus) {
            return std::unexpected<std::string>{bonus.error()};
        }
        if (text == before) {
            continue;
        }
        if (!write_file_bytes(path, text, io_error)) {
            return std::unexpected<std::string>{io_error};
        }
    }
    for (const pending_patch& job : optional) {
        out.emit("skipped optional patch " + job.spec.name + ": endpoint not present in this client");
    }
    if (!pending.empty()) {
        std::string names;
        for (const pending_patch& job : pending) {
            names += names.empty() ? job.spec.name : ", " + job.spec.name;
        }
        return std::unexpected<std::string>{"unsupported client; missing patches: " + names};
    }
    return std::monostate{};
}
} // namespace wemod
