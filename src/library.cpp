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
#include <string_view>
#include <utility>

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

// ── forwarded functions ──────────────────────────────────────────────
// Each entry declares the function signature, the fallback return
// value, and the parameter names. The forwarding stubs are generated
// at the bottom via a macro expansion over this list.

#define VERSION_EXPORTS(X)                                                       \
    X(GetFileVersionInfoA,        BOOL,  FALSE,                                  \
      (LPCSTR, DWORD, DWORD, LPVOID), (a, b, c, d))                             \
    X(GetFileVersionInfoExA,      BOOL,  FALSE,                                  \
      (DWORD, LPCSTR, DWORD, DWORD, LPVOID), (a, b, c, d, e))                   \
    X(GetFileVersionInfoExW,      BOOL,  FALSE,                                  \
      (DWORD, LPCWSTR, DWORD, DWORD, LPVOID), (a, b, c, d, e))                   \
    X(GetFileVersionInfoSizeA,    DWORD, 0,                                      \
      (LPCSTR, LPDWORD), (a, b))                                                \
    X(GetFileVersionInfoSizeExA,  DWORD, 0,                                      \
      (DWORD, LPCSTR, LPDWORD), (a, b, c))                                      \
    X(GetFileVersionInfoSizeExW,  DWORD, 0,                                      \
      (DWORD, LPCWSTR, LPDWORD), (a, b, c))                                     \
    X(GetFileVersionInfoSizeW,   DWORD, 0,                                      \
      (LPCWSTR, LPDWORD), (a, b))                                               \
    X(GetFileVersionInfoW,        BOOL,  FALSE,                                  \
      (LPCWSTR, DWORD, DWORD, LPVOID), (a, b, c, d))                            \
    X(VerFindFileA,               DWORD, 0,                                      \
      (DWORD, V_STR_A, V_STR_A, V_STR_A, LPSTR, PUINT, LPSTR, PUINT),           \
      (a, b, c, d, e, f, g, h))                                                 \
    X(VerFindFileW,               DWORD, 0,                                      \
      (DWORD, V_STR_W, V_STR_W, V_STR_W, LPWSTR, PUINT, LPWSTR, PUINT),         \
      (a, b, c, d, e, f, g, h))                                                 \
    X(VerInstallFileA,            DWORD, 0,                                      \
      (DWORD, V_STR_A, V_STR_A, V_STR_A, V_STR_A, V_STR_A, LPSTR, PUINT),       \
      (a, b, c, d, e, f, g, h))                                                 \
    X(VerInstallFileW,            DWORD, 0,                                      \
      (DWORD, V_STR_W, V_STR_W, V_STR_W, V_STR_W, V_STR_W, LPWSTR, PUINT),     \
      (a, b, c, d, e, f, g, h))                                                 \
    X(VerLanguageNameA,           DWORD, 0,                                      \
      (DWORD, LPSTR, DWORD), (a, b, c))                                         \
    X(VerLanguageNameW,           DWORD, 0,                                      \
      (DWORD, LPWSTR, DWORD), (a, b, c))                                        \
    X(VerQueryValueA,             BOOL,  FALSE,                                  \
      (LPCVOID, LPCSTR, LPVOID *, PUINT), (a, b, c, d))                         \
    X(VerQueryValueW,             BOOL,  FALSE,                                  \
      (LPCVOID, LPCWSTR, LPVOID *, PUINT), (a, b, c, d))

// ── function pointer types and forwarding stubs ─────────────────────

#define DECLARE(name, ret, fallback, params, args)                              \
    using name##_fn = ret(WINAPI *) params;                                     \
    static name##_fn p_##name = nullptr;                                        \
    ret WINAPI name params noexcept {                                           \
        if (!p_##name) {                                                        \
            SetLastError(ERROR_PROC_NOT_FOUND);                                  \
            return fallback;                                                    \
        }                                                                       \
        return p_##name args;                                                   \
    }

VERSION_EXPORTS(DECLARE)

BOOL WINAPI GetFileVersionInfoByHandle() {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

// ── load the real version.dll ───────────────────────────────────────

auto load_original() noexcept -> bool {
    std::array<WCHAR, MAX_PATH> path{};
    const UINT n = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
    constexpr std::wstring_view suffix = L"\\version.dll";

    if (n == 0 || n >= path.size() ||
        static_cast<std::size_t>(n) + suffix.size() >= path.size())
        return false;

    std::memcpy(path.data() + n, suffix.data(), suffix.size() * sizeof(WCHAR));
    original_version = LoadLibraryW(path.data());
    if (!original_version) return false;

#define LOAD(name, ...)                                                         \
    p_##name = reinterpret_cast<name##_fn>(                                      \
        GetProcAddress(original_version, #name));

    VERSION_EXPORTS(LOAD)
    return true;
}

} // namespace

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) noexcept {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (!load_original()) return FALSE;
        disable_asar_integrity();
    }
    return TRUE;
}
