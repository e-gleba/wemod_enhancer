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

constexpr std::size_t fuse_index = 4;
constexpr std::byte removed{'r'};
constexpr std::array<std::uint64_t, 4> sentinel{
    0x6E64474B70374C64ULL,
    0x6262503639377A4EULL,
    0x58486D4B4E57516AULL,
    0x5873743942615A42ULL,
};
constexpr std::array<std::ptrdiff_t, 2> scan_offsets{0, 4};

struct fuse_wire_header final {
    std::array<std::byte, sizeof(sentinel)> marker;
    std::uint8_t version;
    std::uint8_t length;
};

static_assert(sizeof(fuse_wire_header) == sizeof(sentinel) + 2);

class [[nodiscard]] page_guard final {
  public:
    explicit page_guard(std::span<std::byte> bytes) noexcept
        : bytes_{bytes},
          writable_{VirtualProtect(bytes_.data(), bytes_.size(), PAGE_READWRITE,
                                   &old_protection_) != FALSE} {}

    ~page_guard() noexcept {
        if (writable_) {
            DWORD ignored{};
            VirtualProtect(bytes_.data(), bytes_.size(), old_protection_,
                           &ignored);
        }
    }

    page_guard(const page_guard &) = delete;
    auto operator=(const page_guard &) -> page_guard & = delete;

    page_guard(page_guard &&) = delete;
    auto operator=(page_guard &&) -> page_guard & = delete;

    [[nodiscard]] explicit operator bool() const noexcept {
        return writable_;
    }

  private:
    std::span<std::byte> bytes_;
    DWORD old_protection_{};
    bool writable_{};
};

[[nodiscard]] constexpr auto align_down(std::uintptr_t value) noexcept
    -> std::uintptr_t {
    return value & ~std::uintptr_t{7};
}

[[nodiscard]] constexpr auto align_up(std::uintptr_t value) noexcept
    -> std::uintptr_t {
    return align_down(value + 7);
}

[[nodiscard]] auto module_image() noexcept -> std::span<std::byte> {
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
                             std::ptrdiff_t offset) noexcept
    -> fuse_wire_header * {
    if (image.size() < sizeof(sentinel)) {
        return nullptr;
    }

    auto const base = reinterpret_cast<std::uintptr_t>(image.data());
    auto const start = align_up(base) + sizeof(std::uint64_t) + offset;
    auto const end = align_down(base + image.size() - sizeof(sentinel)) -
                     sizeof(std::uint64_t) - offset;
    if (start >= end) {
        return nullptr;
    }

    auto *first = reinterpret_cast<const std::uint64_t *>(start);
    auto const count = static_cast<std::size_t>(end - start) /
                       sizeof(std::uint64_t);
    auto words = std::span{first, count};
    auto match = std::ranges::search(words, sentinel);
    if (match.empty()) {
        return nullptr;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    return reinterpret_cast<fuse_wire_header *>(
        const_cast<std::uint64_t *>(std::to_address(match.begin())));
}

[[nodiscard]] auto fuse_at(fuse_wire_header &wire, std::size_t index) noexcept
    -> std::span<std::byte, 1> {
    auto *fuses = reinterpret_cast<std::byte *>(std::addressof(wire)) +
                  sizeof(fuse_wire_header);
    // MSVC STL: fixed-extent span(It, size_type) is explicit.
    return std::span<std::byte, 1>(fuses + index, 1);
}

} // namespace

[[nodiscard]] auto disable_asar_integrity() noexcept -> bool {
    auto image = module_image();
    auto wires = scan_offsets | std::views::transform([image](auto offset) {
                     return find_wire(image, offset);
                 });
    auto match = std::ranges::find_if(wires, [](auto *wire) {
        return wire != nullptr;
    });
    if (match == wires.end()) {
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
