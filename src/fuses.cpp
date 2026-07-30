// SPDX-License-Identifier: MIT
// ASAR integrity-fuse bypass: walks the host process memory for a
// 32-byte sentinel, locates Electron's fuse wire, and flips the
// integrity fuse to "removed" via VirtualProtect.

#include <gsl/gsl>
#include <windows.h>

#include <bit>
#include <cstdint>
#include <span>

namespace {

constexpr auto sentinel_length = 32uz;
constexpr auto fuse_integrity = 0uz;
constexpr auto fuse_removed = static_cast<std::byte>('r');

#if defined(_WIN64)
// 32-byte sentinel split into four uint64_t for fast comparison.
constexpr std::uint64_t s1 = 0x6E64474B70374C64ULL;
constexpr std::uint64_t s2 = 0x6262503639377A4EULL;
constexpr std::uint64_t s3 = 0x58486D4B4E57516AULL;
constexpr std::uint64_t s4 = 0x5873743942615A42ULL;
#endif

struct fuse_wire {
    char sentinel[sentinel_length];
    unsigned char version;
    unsigned char wire_length;
    std::byte fuses[];
};

// Align a pointer up/down to the next/prev 8-byte boundary.
constexpr auto align8(gsl::not_null<const void *> p, int m) noexcept -> std::uintptr_t {
    return ((reinterpret_cast<std::uintptr_t>(p.get()) + 7u) & ~std::uintptr_t{7u}) +
           static_cast<std::uintptr_t>(m) * 8u;
}

#if defined(_WIN64)
auto find_wire(int offset) noexcept -> fuse_wire * {
    auto *base = static_cast<char *>(GetModuleHandleA(nullptr));
    if (!base) return nullptr;

    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    auto *start = reinterpret_cast<std::uint64_t *>(align8(base, 1) + offset);
    auto *end = reinterpret_cast<std::uint64_t *>(
        align8(base + nt->OptionalHeader.SizeOfImage - sentinel_length, -1) - offset);

    for (auto *p = start; p < end; ++p)
        if (p[0] == s1 && p[1] == s2 && p[2] == s3 && p[3] == s4)
            return reinterpret_cast<fuse_wire *>(p);

    return nullptr;
}
#endif

} // namespace

extern "C" BOOL disable_asar_integrity() noexcept {
#if defined(_WIN64)
    auto *wire = find_wire(0) ? find_wire(0) : find_wire(4);
#else
    auto *wire = static_cast<fuse_wire *>(nullptr);
#endif
    if (!wire || wire->version != 1 || wire->wire_length < 5) return FALSE;

    auto *fuse = &wire->fuses[fuse_integrity];
    if (*fuse == fuse_removed) return TRUE;

    DWORD old{};
    if (!VirtualProtect(fuse, 1, PAGE_READWRITE, &old)) return FALSE;
    *fuse = fuse_removed;
    VirtualProtect(fuse, 1, old, &old);
    return TRUE;
}
