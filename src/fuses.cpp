// SPDX-License-Identifier: MIT

#include "fuses.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>

namespace {

constexpr std::size_t sentinel_size = 32;
constexpr std::size_t fuse_index = 4;
constexpr std::byte removed{'r'};
constexpr std::array<std::byte, sentinel_size> sentinel{
    std::byte{'d'}, std::byte{'L'}, std::byte{'7'}, std::byte{'p'},
    std::byte{'K'}, std::byte{'G'}, std::byte{'d'}, std::byte{'n'},
    std::byte{'N'}, std::byte{'z'}, std::byte{'7'}, std::byte{'9'},
    std::byte{'6'}, std::byte{'P'}, std::byte{'b'}, std::byte{'b'},
    std::byte{'j'}, std::byte{'Q'}, std::byte{'W'}, std::byte{'N'},
    std::byte{'K'}, std::byte{'m'}, std::byte{'H'}, std::byte{'X'},
    std::byte{'B'}, std::byte{'Z'}, std::byte{'a'}, std::byte{'B'},
    std::byte{'9'}, std::byte{'t'}, std::byte{'s'}, std::byte{'X'},
};
constexpr std::array<std::size_t, 2> scan_offsets{0, 4};

struct fuse_wire_header final {
    std::array<std::byte, sentinel_size> marker;
    std::uint8_t version;
    std::uint8_t length;
};

static_assert(sizeof(fuse_wire_header) == sentinel_size + 2);

class [[nodiscard]] page_guard final {
  public:
    explicit page_guard(std::span<std::byte> bytes) noexcept
        : bytes{bytes},
          writable{VirtualProtect(bytes.data(), bytes.size(), PAGE_READWRITE,
                                  &old_protection) != FALSE} {}

    ~page_guard() noexcept {
        if (writable) {
            DWORD ignored{};
            VirtualProtect(bytes.data(), bytes.size(), old_protection, &ignored);
        }
    }

    page_guard(const page_guard &) = delete;
    auto operator=(const page_guard &) -> page_guard & = delete;
    page_guard(page_guard &&) = delete;
    auto operator=(page_guard &&) -> page_guard & = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return writable; }

  private:
    std::span<std::byte> bytes;
    DWORD old_protection{};
    bool writable;
};

[[nodiscard]] auto process_image() noexcept -> std::span<std::byte> {
    auto *base = reinterpret_cast<std::byte *>(GetModuleHandleW(nullptr));
    if (base == nullptr) {
        return {};
    }

    auto const *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0) {
        return {};
    }

    auto const *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(
        base + static_cast<std::size_t>(dos->e_lfanew));
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return {};
    }

    return {base, nt->OptionalHeader.SizeOfImage};
}

[[nodiscard]] auto find_wire(std::span<std::byte> image,
                             std::size_t offset) noexcept
    -> fuse_wire_header * {
    if (offset > image.size() ||
        image.size() - offset < sentinel.size() + sizeof(fuse_wire_header)) {
        return nullptr;
    }

    auto match = std::ranges::search(image.subspan(offset), sentinel);
    if (match.empty()) {
        return nullptr;
    }

    return reinterpret_cast<fuse_wire_header *>(std::to_address(match.begin()));
}

[[nodiscard]] auto fuse_at(fuse_wire_header &wire, std::size_t index) noexcept
    -> std::span<std::byte, 1> {
    auto bytes = std::span{reinterpret_cast<std::byte *>(&wire),
                           sizeof(fuse_wire_header) + wire.length};
    return std::span<std::byte, 1>{bytes.subspan(sizeof(fuse_wire_header) + index,
                                                1)};
}

} // namespace

[[nodiscard]] auto disable_asar_integrity() noexcept -> bool {
    auto image = process_image();
    auto candidates = scan_offsets | std::views::transform([image](auto offset) {
                          return find_wire(image, offset);
                      });
    auto match = std::ranges::find_if(candidates, [](auto *wire) {
        return wire != nullptr;
    });
    if (match == candidates.end()) {
        return false;
    }

    auto &wire = **match;
    if (wire.version != 1 || wire.length <= fuse_index) {
        return false;
    }

    auto fuse = fuse_at(wire, fuse_index);
    if (fuse.front() == removed) {
        return true;
    }

    page_guard writable{fuse};
    if (!writable) {
        return false;
    }

    fuse.front() = removed;
    return true;
}
