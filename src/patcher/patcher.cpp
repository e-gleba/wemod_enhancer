#include "patcher.hpp"

#include "asar.hpp"
#include "bundles.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ranges>

namespace wemod
{
namespace
{
constexpr std::string_view fuse_sentinel{"dL7pKGdnNz796PbbjQWNKmHXBZaB9tsX"};
constexpr std::size_t fuse_meta_size{34UZ};
constexpr std::size_t fuse_index{4UZ};
constexpr char fuse_off{'0'};
constexpr char fuse_on{'1'};
constexpr char fuse_removed{'r'};

/// Adapts a log_sink callback to the line_sink interface.
class sink_adapter final : public line_sink
{
  public:
    explicit sink_adapter(std::vector<std::string>& lines, const log_sink& on_line)
        : lines_{lines}, on_line_{on_line}
    {
    }

    void emit(const std::string& line) override
    {
        lines_.push_back(line);
        if (on_line_) {
            on_line_(line);
        }
    }

  private:
    std::vector<std::string>& lines_;
    const log_sink& on_line_;
};

[[nodiscard]] patch_error fail(const std::string& message)
{
    return patch_error{.message = message};
}

// mkdtemp needs a mutable template; the trailing XXXXXX is replaced in
// place. RAII removes the directory unless released after success.
// See: https://man7.org/linux/man-pages/man3/mkdtemp.3.html
class temp_workdir final
{
  public:
    [[nodiscard]] static std::expected<temp_workdir, std::string> create()
    {
        std::error_code error;
        const fs::path anchor{fs::temp_directory_path(error)};
        if (error) {
            return std::unexpected<std::string>{"cannot locate temp dir"};
        }
        std::string pattern{(anchor / "wemod-enhancer-XXXXXX").string()};
        if (::mkdtemp(pattern.data()) == nullptr) {
            return std::unexpected<std::string>{"cannot create temp dir"};
        }
        return temp_workdir{fs::path{pattern}};
    }

    temp_workdir(const temp_workdir&) = delete;
    temp_workdir& operator=(const temp_workdir&) = delete;

    temp_workdir(temp_workdir&& other) noexcept : path_{std::move(other.path_)}, owned_{other.owned_}
    {
        other.owned_ = false;
    }

    ~temp_workdir()
    {
        if (owned_) {
            std::error_code remove_error;
            fs::remove_all(path_, remove_error);
        }
    }

    [[nodiscard]] const fs::path& path() const noexcept
    {
        return path_;
    }

    void release() noexcept
    {
        owned_ = false;
    }

  private:
    explicit temp_workdir(fs::path path) : path_{std::move(path)}
    {
    }

    fs::path path_;
    bool owned_{true};
};

[[nodiscard]] std::size_t fuse_wire_of(const std::string& data) noexcept
{
    std::size_t start{0UZ};
    while (true) {
        const std::size_t found{data.find(fuse_sentinel, start)};
        if (found == std::string::npos) {
            return std::string::npos;
        }
        const auto* bytes{reinterpret_cast<const unsigned char*>(data.data() + found)};
        const bool valid{found + fuse_meta_size <= data.size() && bytes[32UZ] == 1U && bytes[33UZ] >= 5U};
        if (valid) {
            return found;
        }
        start = found + 1UZ;
        if (start >= data.size()) {
            return std::string::npos;
        }
    }
}

[[nodiscard]] std::expected<char, std::string> fuse_state_bytes(const std::string& data)
{
    const std::size_t found{fuse_wire_of(data)};
    if (found == std::string::npos) {
        return std::unexpected<std::string>{"no fuse wire"};
    }
    return data[found + fuse_meta_size + fuse_index];
}

[[nodiscard]] std::expected<char, std::string> read_fuse_state(const fs::path& exe)
{
    std::string data;
    std::string error;
    if (!read_file_bytes(exe, data, error).empty()) {
        return std::unexpected<std::string>{error};
    }
    return fuse_state_bytes(data);
}

[[nodiscard]] std::expected<char, std::string> write_fuse_state(const fs::path& exe, const char state)
{
    const std::array valid{fuse_off, fuse_on, fuse_removed};
    if (std::ranges::find(valid, state) == valid.end()) {
        return std::unexpected<std::string>{std::string{"invalid fuse state '"} + state + "'"};
    }
    std::string data;
    std::string error;
    if (!read_file_bytes(exe, data, error).empty()) {
        return std::unexpected<std::string>{error};
    }
    const std::size_t found{fuse_wire_of(data)};
    if (found == std::string::npos) {
        return std::unexpected<std::string>{"electron fuse wire not found in " + exe.string()};
    }
    const std::size_t pos{found + fuse_meta_size + fuse_index};
    const char previous{data[pos]};
    if (std::ranges::find(valid, previous) == valid.end()) {
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
    const std::array names{std::string{"Wand.exe"}, std::string{"WeMod.exe"}};
    for (const std::string& name : names) {
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

void cleanup_legacy_dll(const fs::path& install, line_sink& out)
{
    const fs::path dll{install / "version.dll"};
    const fs::path dll_backup{install / "version.dll.wemod-enhancer-backup"};
    std::error_code probe;
    if (fs::is_regular_file(dll_backup, probe) && !probe) {
        std::error_code copy_error;
        fs::copy_file(dll_backup, dll, fs::copy_options::overwrite_existing, copy_error);
        std::error_code remove_error;
        fs::remove(dll_backup, remove_error);
        out.emit("restored pre-proxy version.dll");
        return;
    }
    probe.clear();
    if (fs::is_regular_file(dll, probe) && !probe) {
        out.emit("kept unexpected version.dll - remove manually if it is the retired proxy");
    }
}

[[nodiscard]] std::expected<bool, std::string> flip_fuse(const fs::path& exe, line_sink& out)
{
    auto state{read_fuse_state(exe)};
    if (!state) {
        out.emit("skipped fuse flip: no electron fuse wire in " + exe.filename().string());
        return false;
    }
    if (*state == fuse_removed) {
        out.emit("asar integrity fuse already removed in " + exe.filename().string());
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
    out.emit("removed asar integrity fuse in " + exe.filename().string() + " (was '" + std::string(1UZ, *previous) +
             "')");
    return true;
}

void restore_fuse(const fs::path& exe, line_sink& out)
{
    const fs::path backup{exe_backup_path(exe)};
    const fs::path legacy{exe.parent_path() / "electron-fuse.wemod-enhancer-backup"};
    std::error_code probe;
    if (fs::is_regular_file(backup, probe) && !probe) {
        std::error_code copy_error;
        fs::copy_file(backup, exe, fs::copy_options::overwrite_existing, copy_error);
        std::error_code remove_error;
        fs::remove(backup, remove_error);
        out.emit("restored " + exe.filename().string() + " from backup");
        return;
    }
    probe.clear();
    if (!fs::is_regular_file(legacy, probe) || probe) {
        return;
    }
    std::string stored;
    std::string error;
    if (!read_file_bytes(legacy, stored, error).empty()) {
        std::error_code remove_error;
        fs::remove(legacy, remove_error);
        return;
    }
    std::string trimmed;
    std::ranges::copy_if(stored, std::back_inserter(trimmed),
                         [](const char piece) { return !std::isspace(static_cast<unsigned char>(piece)); });
    auto current{read_fuse_state(exe)};
    if (!trimmed.empty() && current && *current == fuse_removed) {
        auto result{write_fuse_state(exe, trimmed.front())};
        if (result) {
            out.emit("restored asar integrity fuse in " + exe.filename().string() + " (was '" +
                     std::string(1UZ, trimmed.front()) + "')");
        }
    }
    std::error_code remove_error;
    fs::remove(legacy, remove_error);
}

// Failure helper: restore the asar from backup, undo a flipped fuse.
void roll_back(const fs::path& backup, const fs::path& asar, const std::optional<fs::path>& exe, bool exe_touched,
               line_sink& out)
{
    auto rolled{copy_file_over(backup, asar)};
    (void)rolled;
    if (exe && exe_touched) {
        restore_fuse(*exe, out);
    }
}

[[nodiscard]] patch_result run_patch(const fs::path& install, sink_adapter& out)
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
        // First run: keep pristine bytes; later runs restart from backup.
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
        auto work{temp_workdir::create()};
        if (!work) {
            return std::unexpected<patch_error>{fail(work.error())};
        }
        std::string raw;
        std::string error;
        if (!read_file_bytes(asar, raw, error).empty()) {
            return std::unexpected<patch_error>{fail(error)};
        }
        auto parsed{parse_asar(raw)};
        if (!parsed) {
            return std::unexpected<patch_error>{fail(parsed.error())};
        }
        auto extracted{extract_asar(asar, work->path(), parsed->view.header, parsed->view.data_offset)};
        if (!extracted) {
            roll_back(backup, asar, exe, exe_touched, out);
            return std::unexpected<patch_error>{fail(extracted.error())};
        }
        auto patched{patch_bundles(work->path(), out)};
        if (!patched) {
            roll_back(backup, asar, exe, exe_touched, out);
            return std::unexpected<patch_error>{fail(patched.error())};
        }
        auto packed{pack_asar(work->path(), parsed->view.header)};
        if (!packed) {
            roll_back(backup, asar, exe, exe_touched, out);
            return std::unexpected<patch_error>{fail(packed.error())};
        }
        const fs::path tmp_out{fs::path{asar.string() + ".tmp"}};
        if (!write_file_bytes(tmp_out, *packed, error)) {
            roll_back(backup, asar, exe, exe_touched, out);
            return std::unexpected<patch_error>{fail(error)};
        }
        std::error_code rename_error;
        fs::rename(tmp_out, asar, rename_error);
        if (rename_error) {
            std::error_code remove_error;
            fs::remove(tmp_out, remove_error);
            roll_back(backup, asar, exe, exe_touched, out);
            return std::unexpected<patch_error>{fail("cannot replace " + asar.string())};
        }
        cleanup_legacy_dll(install, out);
        if (exe) {
            auto flipped{flip_fuse(*exe, out)};
            if (!flipped) {
                roll_back(backup, asar, exe, exe_touched, out);
                return std::unexpected<patch_error>{fail(flipped.error())};
            }
            exe_touched = *flipped;
        } else {
            out.emit("skipped fuse flip: no Wand.exe/WeMod.exe next to resources");
        }
        out.emit("patched " + install.string());
    } catch (const std::exception& failure) {
        roll_back(backup, asar, exe, exe_touched, out);
        return std::unexpected<patch_error>{fail(failure.what())};
    }
    return patch_report{.install_dir = install, .log_lines = {}, .fuse_removed = exe_touched};
}

[[nodiscard]] patch_result run_restore(const fs::path& install, sink_adapter& out)
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
    out.emit("restored " + install.string());
    return patch_report{.install_dir = install, .log_lines = {}, .fuse_removed = false};
}

[[nodiscard]] patch_result finish(patch_result result, std::vector<std::string>& lines)
{
    if (!result) {
        return result;
    }
    result->log_lines = lines;
    return result;
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
    return std::string(1UZ, *state);
}

patch_result patch_install(const fs::path& install_dir)
{
    return patch_install(install_dir, {});
}

patch_result restore_install(const fs::path& install_dir)
{
    return restore_install(install_dir, {});
}

patch_result patch_install(const fs::path& install_dir, const log_sink& on_line)
{
    std::vector<std::string> lines;
    sink_adapter out{lines, on_line};
    return finish(run_patch(install_dir, out), lines);
}

patch_result restore_install(const fs::path& install_dir, const log_sink& on_line)
{
    std::vector<std::string> lines;
    sink_adapter out{lines, on_line};
    return finish(run_restore(install_dir, out), lines);
}
} // namespace wemod
