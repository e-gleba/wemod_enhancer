// SPDX-License-Identifier: MIT
// version.dll proxy: forwards 16 Windows version-info API calls to
// the real version.dll, then disables ASAR integrity on load.

// NOVERSION suppresses version function declarations in the MSVC SDK.
// MinGW-w64's winver.h does NOT honour NOVERSION, so we must also
// prevent winver.h from being included when building with MinGW.
#define NOVERSION
#ifdef __MINGW32__
  #define VER_H 1
  #define _WINVER_H 1
#endif

#include <gsl/gsl>
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <ranges>
#include <string_view>

extern "C" BOOL disable_asar_integrity() noexcept;

namespace {

gsl::owner<HMODULE> original_version = nullptr;

// MinGW-w64 declares Ver* params as non-const (LPSTR/LPWSTR).
// MSVC SDK declares them const-correct (LPCSTR/LPCWSTR).
#ifdef __MINGW32__
  #define V_STR_A LPSTR
  #define V_STR_W LPWSTR
#else
  #define V_STR_A LPCSTR
  #define V_STR_W LPCWSTR
#endif

// ── function pointer types ───────────────────────────────────────────

using GetFileVersionInfoA_fn = BOOL(WINAPI *)(LPCSTR, DWORD, DWORD, LPVOID);
using GetFileVersionInfoW_fn = BOOL(WINAPI *)(LPCWSTR, DWORD, DWORD, LPVOID);
using GetFileVersionInfoExA_fn = BOOL(WINAPI *)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
using GetFileVersionInfoExW_fn = BOOL(WINAPI *)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
using GetFileVersionInfoSizeA_fn = DWORD(WINAPI *)(LPCSTR, LPDWORD);
using GetFileVersionInfoSizeW_fn = DWORD(WINAPI *)(LPCWSTR, LPDWORD);
using GetFileVersionInfoSizeExA_fn = DWORD(WINAPI *)(DWORD, LPCSTR, LPDWORD);
using GetFileVersionInfoSizeExW_fn = DWORD(WINAPI *)(DWORD, LPCWSTR, LPDWORD);
using VerFindFileA_fn = DWORD(WINAPI *)(DWORD, V_STR_A, V_STR_A, V_STR_A, LPSTR, PUINT, LPSTR, PUINT);
using VerFindFileW_fn = DWORD(WINAPI *)(DWORD, V_STR_W, V_STR_W, V_STR_W, LPWSTR, PUINT, LPWSTR, PUINT);
using VerInstallFileA_fn = DWORD(WINAPI *)(DWORD, V_STR_A, V_STR_A, V_STR_A, V_STR_A, V_STR_A, LPSTR, PUINT);
using VerInstallFileW_fn = DWORD(WINAPI *)(DWORD, V_STR_W, V_STR_W, V_STR_W, V_STR_W, V_STR_W, LPWSTR, PUINT);
using VerLanguageNameA_fn = DWORD(WINAPI *)(DWORD, LPSTR, DWORD);
using VerLanguageNameW_fn = DWORD(WINAPI *)(DWORD, LPWSTR, DWORD);
using VerQueryValueA_fn = BOOL(WINAPI *)(LPCVOID, LPCSTR, LPVOID *, PUINT);
using VerQueryValueW_fn = BOOL(WINAPI *)(LPCVOID, LPCWSTR, LPVOID *, PUINT);

// ── function pointer storage ─────────────────────────────────────────

static GetFileVersionInfoA_fn p_GetFileVersionInfoA = nullptr;
static GetFileVersionInfoW_fn p_GetFileVersionInfoW = nullptr;
static GetFileVersionInfoExA_fn p_GetFileVersionInfoExA = nullptr;
static GetFileVersionInfoExW_fn p_GetFileVersionInfoExW = nullptr;
static GetFileVersionInfoSizeA_fn p_GetFileVersionInfoSizeA = nullptr;
static GetFileVersionInfoSizeW_fn p_GetFileVersionInfoSizeW = nullptr;
static GetFileVersionInfoSizeExA_fn p_GetFileVersionInfoSizeExA = nullptr;
static GetFileVersionInfoSizeExW_fn p_GetFileVersionInfoSizeExW = nullptr;
static VerFindFileA_fn p_VerFindFileA = nullptr;
static VerFindFileW_fn p_VerFindFileW = nullptr;
static VerInstallFileA_fn p_VerInstallFileA = nullptr;
static VerInstallFileW_fn p_VerInstallFileW = nullptr;
static VerLanguageNameA_fn p_VerLanguageNameA = nullptr;
static VerLanguageNameW_fn p_VerLanguageNameW = nullptr;
static VerQueryValueA_fn p_VerQueryValueA = nullptr;
static VerQueryValueW_fn p_VerQueryValueW = nullptr;

// ── forwarding stubs ─────────────────────────────────────────────────
// Each stub checks its function pointer, returns a fallback if the
// real function was not found, otherwise forwards the call.

extern "C" BOOL WINAPI GetFileVersionInfoA(LPCSTR a, DWORD b, DWORD c, LPVOID d) noexcept {
    if (!p_GetFileVersionInfoA) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    return p_GetFileVersionInfoA(a, b, c, d);
}

extern "C" BOOL WINAPI GetFileVersionInfoW(LPCWSTR a, DWORD b, DWORD c, LPVOID d) noexcept {
    if (!p_GetFileVersionInfoW) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    return p_GetFileVersionInfoW(a, b, c, d);
}

extern "C" BOOL WINAPI GetFileVersionInfoExA(DWORD a, LPCSTR b, DWORD c, DWORD d, LPVOID e) noexcept {
    if (!p_GetFileVersionInfoExA) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    return p_GetFileVersionInfoExA(a, b, c, d, e);
}

extern "C" BOOL WINAPI GetFileVersionInfoExW(DWORD a, LPCWSTR b, DWORD c, DWORD d, LPVOID e) noexcept {
    if (!p_GetFileVersionInfoExW) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    return p_GetFileVersionInfoExW(a, b, c, d, e);
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR a, LPDWORD b) noexcept {
    if (!p_GetFileVersionInfoSizeA) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return 0;
    }
    return p_GetFileVersionInfoSizeA(a, b);
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR a, LPDWORD b) noexcept {
    if (!p_GetFileVersionInfoSizeW) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return 0;
    }
    return p_GetFileVersionInfoSizeW(a, b);
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExA(DWORD a, LPCSTR b, LPDWORD c) noexcept {
    if (!p_GetFileVersionInfoSizeExA) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return 0;
    }
    return p_GetFileVersionInfoSizeExA(a, b, c);
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExW(DWORD a, LPCWSTR b, LPDWORD c) noexcept {
    if (!p_GetFileVersionInfoSizeExW) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return 0;
    }
    return p_GetFileVersionInfoSizeExW(a, b, c);
}

extern "C" DWORD WINAPI VerFindFileA(DWORD a, V_STR_A b, V_STR_A c, V_STR_A d, LPSTR e, PUINT f, LPSTR g, PUINT h) noexcept {
    if (!p_VerFindFileA) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return 0;
    }
    return p_VerFindFileA(a, b, c, d, e, f, g, h);
}

extern "C" DWORD WINAPI VerFindFileW(DWORD a, V_STR_W b, V_STR_W c, V_STR_W d, LPWSTR e, PUINT f, LPWSTR g, PUINT h) noexcept {
    if (!p_VerFindFileW) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return 0;
    }
    return p_VerFindFileW(a, b, c, d, e, f, g, h);
}

extern "C" DWORD WINAPI VerInstallFileA(DWORD a, V_STR_A b, V_STR_A c, V_STR_A d, V_STR_A e, V_STR_A f, LPSTR g, PUINT h) noexcept {
    if (!p_VerInstallFileA) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return 0;
    }
    return p_VerInstallFileA(a, b, c, d, e, f, g, h);
}

extern "C" DWORD WINAPI VerInstallFileW(DWORD a, V_STR_W b, V_STR_W c, V_STR_W d, V_STR_W e, V_STR_W f, LPWSTR g, PUINT h) noexcept {
    if (!p_VerInstallFileW) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return 0;
    }
    return p_VerInstallFileW(a, b, c, d, e, f, g, h);
}

extern "C" DWORD WINAPI VerLanguageNameA(DWORD a, LPSTR b, DWORD c) noexcept {
    if (!p_VerLanguageNameA) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return 0;
    }
    return p_VerLanguageNameA(a, b, c);
}

extern "C" DWORD WINAPI VerLanguageNameW(DWORD a, LPWSTR b, DWORD c) noexcept {
    if (!p_VerLanguageNameW) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return 0;
    }
    return p_VerLanguageNameW(a, b, c);
}

extern "C" BOOL WINAPI VerQueryValueA(LPCVOID a, LPCSTR b, LPVOID *c, PUINT d) noexcept {
    if (!p_VerQueryValueA) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    return p_VerQueryValueA(a, b, c, d);
}

extern "C" BOOL WINAPI VerQueryValueW(LPCVOID a, LPCWSTR b, LPVOID *c, PUINT d) noexcept {
    if (!p_VerQueryValueW) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }
    return p_VerQueryValueW(a, b, c, d);
}

extern "C" BOOL WINAPI GetFileVersionInfoByHandle() noexcept {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

// ── export table for loading ─────────────────────────────────────────
// A const array of name → pointer-slot pairs. Used with
// std::ranges::for_each to resolve all exports in one pass —
// no macros needed for the load step.

struct export_entry {
    const char *name;
    FARPROC *target;
};

const std::array exports = {
    export_entry{"GetFileVersionInfoA", reinterpret_cast<FARPROC *>(&p_GetFileVersionInfoA)},
    export_entry{"GetFileVersionInfoW", reinterpret_cast<FARPROC *>(&p_GetFileVersionInfoW)},
    export_entry{"GetFileVersionInfoExA", reinterpret_cast<FARPROC *>(&p_GetFileVersionInfoExA)},
    export_entry{"GetFileVersionInfoExW", reinterpret_cast<FARPROC *>(&p_GetFileVersionInfoExW)},
    export_entry{"GetFileVersionInfoSizeA", reinterpret_cast<FARPROC *>(&p_GetFileVersionInfoSizeA)},
    export_entry{"GetFileVersionInfoSizeW", reinterpret_cast<FARPROC *>(&p_GetFileVersionInfoSizeW)},
    export_entry{"GetFileVersionInfoSizeExA", reinterpret_cast<FARPROC *>(&p_GetFileVersionInfoSizeExA)},
    export_entry{"GetFileVersionInfoSizeExW", reinterpret_cast<FARPROC *>(&p_GetFileVersionInfoSizeExW)},
    export_entry{"VerFindFileA", reinterpret_cast<FARPROC *>(&p_VerFindFileA)},
    export_entry{"VerFindFileW", reinterpret_cast<FARPROC *>(&p_VerFindFileW)},
    export_entry{"VerInstallFileA", reinterpret_cast<FARPROC *>(&p_VerInstallFileA)},
    export_entry{"VerInstallFileW", reinterpret_cast<FARPROC *>(&p_VerInstallFileW)},
    export_entry{"VerLanguageNameA", reinterpret_cast<FARPROC *>(&p_VerLanguageNameA)},
    export_entry{"VerLanguageNameW", reinterpret_cast<FARPROC *>(&p_VerLanguageNameW)},
    export_entry{"VerQueryValueA", reinterpret_cast<FARPROC *>(&p_VerQueryValueA)},
    export_entry{"VerQueryValueW", reinterpret_cast<FARPROC *>(&p_VerQueryValueW)},
};

// ── load the real version.dll ───────────────────────────────────────

auto load_original() noexcept -> bool {
    std::array<WCHAR, MAX_PATH> path{};
    const UINT n = GetSystemDirectoryW(path.data(),
                                       static_cast<UINT>(path.size()));
    constexpr std::wstring_view suffix = L"\\version.dll";

    if (n == 0 || n >= path.size() ||
        static_cast<std::size_t>(n) + suffix.size() >= path.size()) {
        return false;
    }

    std::memcpy(path.data() + n, suffix.data(), suffix.size() * sizeof(WCHAR));
    original_version = LoadLibraryW(path.data());
    if (!original_version) {
        return false;
    }

    std::ranges::for_each(exports, [&](const auto &entry) {
        *entry.target = GetProcAddress(original_version, entry.name);
    });

    return true;
}

} // namespace

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) noexcept {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (!load_original()) {
            return FALSE;
        }
        disable_asar_integrity();
    }
    return TRUE;
}
