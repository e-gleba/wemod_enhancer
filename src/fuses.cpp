// SPDX-License-Identifier: MIT
// ASAR integrity-fuse bypass: walks the host process memory for a
// 32-byte sentinel, locates Electron's fuse wire, and flips the
// integrity fuse to "removed" via VirtualProtect.

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>
#include <span>

namespace {

// ── compile-time constants ──────────────────────────────────────────

constexpr auto sentinel_length = 32uz;
constexpr auto fuse_integrity = 0uz;
constexpr auto fuse_removed = std::byte{'r'};

#if defined(_WIN64)
// 32-byte sentinel split into four uint64_t for fast comparison.
constexpr std::array sentinel{
    0x6E64474B70374C64ULL,
    0x6262503639377A4EULL,
    0x58486D4B4E57516AULL,
    0x5873743942615A42ULL,
};
#endif

// ── fuse wire layout ────────────────────────────────────────────────

struct fuse_wire {
    char sentinel[sentinel_length];
    unsigned char version;
    unsigned char wire_length;
    std::byte fuses[];
};

static_assert(sizeof(sentinel) == sentinel_length);

// ── memory helpers ──────────────────────────────────────────────────

// Align a pointer up/down to the next/prev 8-byte boundary.
constexpr auto align8(std::uintptr_t p, int m) noexcept -> std::uintptr_t {
    return ((p + 7u) & ~std::uintptr_t{7u}) +
           static_cast<std::uintptr_t>(m) * 8u;
}

// RAII guard: makes a memory page writable on construction,
// restores the original protection on destruction.
class [[nodiscard]] page_guard final {
  public:
    page_guard() = default;
    explicit page_guard(void *addr, std::size_t size) noexcept
        : addr_{addr}, size_{size} {
        ok_ = VirtualProtect(addr, size, PAGE_READWRITE, &old_);
    }
    ~page_guard() noexcept {
        if (ok_) { DWORD tmp; VirtualProtect(addr_, size_, old_, &tmp); }
    }
    page_guard(const page_guard &) = delete;
    auto operator=(const page_guard &) -> page_guard & = delete;
    explicit operator bool() const noexcept { return ok_; }

  private:
    void *addr_{};
    std::size_t size_{};
    DWORD old_{};
    bool ok_{false};
};

#if defined(_WIN64)
// View the module image as a span of uint64_t and locate the
// sentinel using std::ranges::search — one algorithm call.
auto find_wire(int offset) noexcept -> fuse_wire * {
    auto *base = static_cast<char *>(GetModuleHandleA(nullptr));
    if (!base) return nullptr;

    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    auto start = align8(reinterpret_cast<std::uintptr_t>(base), 1) + offset;
    auto end = align8(reinterpret_cast<std::uintptr_t>(base) +
                          nt->OptionalHeader.SizeOfImage - sentinel_length,
                      -1) - offset;
    if (start >= end) return nullptr;

    auto count = static_cast<std::size_t>(end - start) / sizeof(std::uint64_t);
    auto haystack = std::span{reinterpret_cast<const std::uint64_t *>(start),
                               count};

    auto it = std::ranges::search(haystack, sentinel);
    if (it.begin() == haystack.end()) return nullptr;
    return reinterpret_cast<fuse_wire *>(
        const_cast<std::uint64_t *>(it.begin().operator->()));
}
#endif

} // namespace

extern "C" BOOL disable_asar_integrity() noexcept {
#if defined(_WIN64)
    // Try both alignment offsets; the first match wins.
    constexpr std::array offsets{0, 4};
    auto found = std::ranges::find_if(offsets, [&](int off) {
        return find_wire(off) != nullptr;
    });
    auto *wire = found == offsets.end() ? nullptr : find_wire(*found);
#else
    auto *wire = static_cast<fuse_wire *>(nullptr);
#endif
    if (!wire || wire->version != 1 || wire->wire_length < 5) return FALSE;

    auto *fuse = &wire->fuses[fuse_integrity];
    if (*fuse == fuse_removed) return TRUE;

    page_guard guard{fuse, 1};
    if (!guard) return FALSE;

    *fuse = fuse_removed;
    return TRUE;
}
