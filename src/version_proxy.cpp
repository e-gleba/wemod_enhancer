// SPDX-License-Identifier: MIT

// Suppress Windows SDK declarations: this file defines the proxy exports.
#define NOVERSION
#ifdef __MINGW32__
  #define VER_H 1
  #define _WINVER_H 1
#endif

#include "proxy.hpp"

#include <gsl/gsl>

#include <array>
#include <cstring>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

#ifdef __MINGW32__
inline constexpr bool mingw = true;
#else
inline constexpr bool mingw = false;
#endif

using ver_string_a = std::conditional_t<mingw, LPSTR, LPCSTR>;
using ver_string_w = std::conditional_t<mingw, LPWSTR, LPCWSTR>;

gsl::owner<HMODULE> original_version{};

template<typename Function>
[[nodiscard]] auto resolve(const char *name) noexcept -> Function {
    return reinterpret_cast<Function>(GetProcAddress(original_version, name));
}

template<auto &Function, typename Fallback, typename... Args>
[[nodiscard]] auto forward(Fallback fallback, Args &&...args) noexcept
    -> std::invoke_result_t<decltype(Function), Args...> {
    if (Function == nullptr) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return fallback;
    }

    return Function(std::forward<Args>(args)...);
}

using get_info_a_fn = BOOL(WINAPI *)(LPCSTR, DWORD, DWORD, LPVOID);
using get_info_w_fn = BOOL(WINAPI *)(LPCWSTR, DWORD, DWORD, LPVOID);
using get_info_ex_a_fn = BOOL(WINAPI *)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
using get_info_ex_w_fn = BOOL(WINAPI *)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
using get_size_a_fn = DWORD(WINAPI *)(LPCSTR, LPDWORD);
using get_size_w_fn = DWORD(WINAPI *)(LPCWSTR, LPDWORD);
using get_size_ex_a_fn = DWORD(WINAPI *)(DWORD, LPCSTR, LPDWORD);
using get_size_ex_w_fn = DWORD(WINAPI *)(DWORD, LPCWSTR, LPDWORD);
using find_a_fn = DWORD(WINAPI *)(DWORD, ver_string_a, ver_string_a,
                                  ver_string_a, LPSTR, PUINT, LPSTR, PUINT);
using find_w_fn = DWORD(WINAPI *)(DWORD, ver_string_w, ver_string_w,
                                  ver_string_w, LPWSTR, PUINT, LPWSTR, PUINT);
using install_a_fn = DWORD(WINAPI *)(DWORD, ver_string_a, ver_string_a,
                                     ver_string_a, ver_string_a, ver_string_a,
                                     LPSTR, PUINT);
using install_w_fn = DWORD(WINAPI *)(DWORD, ver_string_w, ver_string_w,
                                     ver_string_w, ver_string_w, ver_string_w,
                                     LPWSTR, PUINT);
using language_a_fn = DWORD(WINAPI *)(DWORD, LPSTR, DWORD);
using language_w_fn = DWORD(WINAPI *)(DWORD, LPWSTR, DWORD);
using query_a_fn = BOOL(WINAPI *)(LPCVOID, LPCSTR, LPVOID *, PUINT);
using query_w_fn = BOOL(WINAPI *)(LPCVOID, LPCWSTR, LPVOID *, PUINT);

get_info_a_fn get_info_a{};
get_info_w_fn get_info_w{};
get_info_ex_a_fn get_info_ex_a{};
get_info_ex_w_fn get_info_ex_w{};
get_size_a_fn get_size_a{};
get_size_w_fn get_size_w{};
get_size_ex_a_fn get_size_ex_a{};
get_size_ex_w_fn get_size_ex_w{};
find_a_fn find_a{};
find_w_fn find_w{};
install_a_fn install_a{};
install_w_fn install_w{};
language_a_fn language_a{};
language_w_fn language_w{};
query_a_fn query_a{};
query_w_fn query_w{};

} // namespace

extern "C" BOOL WINAPI GetFileVersionInfoA(LPCSTR a, DWORD b, DWORD c,
                                             LPVOID d) noexcept {
    return forward<get_info_a>(FALSE, a, b, c, d);
}

extern "C" BOOL WINAPI GetFileVersionInfoW(LPCWSTR a, DWORD b, DWORD c,
                                             LPVOID d) noexcept {
    return forward<get_info_w>(FALSE, a, b, c, d);
}

extern "C" BOOL WINAPI GetFileVersionInfoExA(DWORD a, LPCSTR b, DWORD c,
                                               DWORD d, LPVOID e) noexcept {
    return forward<get_info_ex_a>(FALSE, a, b, c, d, e);
}

extern "C" BOOL WINAPI GetFileVersionInfoExW(DWORD a, LPCWSTR b, DWORD c,
                                               DWORD d, LPVOID e) noexcept {
    return forward<get_info_ex_w>(FALSE, a, b, c, d, e);
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR a,
                                                  LPDWORD b) noexcept {
    return forward<get_size_a>(DWORD{}, a, b);
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR a,
                                                  LPDWORD b) noexcept {
    return forward<get_size_w>(DWORD{}, a, b);
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExA(DWORD a, LPCSTR b,
                                                    LPDWORD c) noexcept {
    return forward<get_size_ex_a>(DWORD{}, a, b, c);
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExW(DWORD a, LPCWSTR b,
                                                    LPDWORD c) noexcept {
    return forward<get_size_ex_w>(DWORD{}, a, b, c);
}

extern "C" DWORD WINAPI VerFindFileA(DWORD a, ver_string_a b, ver_string_a c,
                                      ver_string_a d, LPSTR e, PUINT f, LPSTR g,
                                      PUINT h) noexcept {
    return forward<find_a>(DWORD{}, a, b, c, d, e, f, g, h);
}

extern "C" DWORD WINAPI VerFindFileW(DWORD a, ver_string_w b, ver_string_w c,
                                      ver_string_w d, LPWSTR e, PUINT f,
                                      LPWSTR g, PUINT h) noexcept {
    return forward<find_w>(DWORD{}, a, b, c, d, e, f, g, h);
}

extern "C" DWORD WINAPI VerInstallFileA(
    DWORD a, ver_string_a b, ver_string_a c, ver_string_a d, ver_string_a e,
    ver_string_a f, LPSTR g, PUINT h) noexcept {
    return forward<install_a>(DWORD{}, a, b, c, d, e, f, g, h);
}

extern "C" DWORD WINAPI VerInstallFileW(
    DWORD a, ver_string_w b, ver_string_w c, ver_string_w d, ver_string_w e,
    ver_string_w f, LPWSTR g, PUINT h) noexcept {
    return forward<install_w>(DWORD{}, a, b, c, d, e, f, g, h);
}

extern "C" DWORD WINAPI VerLanguageNameA(DWORD a, LPSTR b,
                                           DWORD c) noexcept {
    return forward<language_a>(DWORD{}, a, b, c);
}

extern "C" DWORD WINAPI VerLanguageNameW(DWORD a, LPWSTR b,
                                           DWORD c) noexcept {
    return forward<language_w>(DWORD{}, a, b, c);
}

extern "C" BOOL WINAPI VerQueryValueA(LPCVOID a, LPCSTR b, LPVOID *c,
                                        PUINT d) noexcept {
    return forward<query_a>(FALSE, a, b, c, d);
}

extern "C" BOOL WINAPI VerQueryValueW(LPCVOID a, LPCWSTR b, LPVOID *c,
                                        PUINT d) noexcept {
    return forward<query_w>(FALSE, a, b, c, d);
}

extern "C" BOOL WINAPI GetFileVersionInfoByHandle() noexcept {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

[[nodiscard]] auto load_version_proxy() noexcept -> bool {
    std::array<WCHAR, MAX_PATH> path{};
    auto const size = GetSystemDirectoryW(path.data(),
                                          static_cast<UINT>(path.size()));
    constexpr std::wstring_view suffix{L"\\version.dll"};
    if (size == 0 || size >= path.size() ||
        size + suffix.size() >= path.size()) {
        return false;
    }

    std::memcpy(path.data() + size, suffix.data(),
                suffix.size() * sizeof(WCHAR));
    original_version = LoadLibraryW(path.data());
    if (original_version == nullptr) {
        return false;
    }

    get_info_a = resolve<get_info_a_fn>("GetFileVersionInfoA");
    get_info_w = resolve<get_info_w_fn>("GetFileVersionInfoW");
    get_info_ex_a = resolve<get_info_ex_a_fn>("GetFileVersionInfoExA");
    get_info_ex_w = resolve<get_info_ex_w_fn>("GetFileVersionInfoExW");
    get_size_a = resolve<get_size_a_fn>("GetFileVersionInfoSizeA");
    get_size_w = resolve<get_size_w_fn>("GetFileVersionInfoSizeW");
    get_size_ex_a = resolve<get_size_ex_a_fn>("GetFileVersionInfoSizeExA");
    get_size_ex_w = resolve<get_size_ex_w_fn>("GetFileVersionInfoSizeExW");
    find_a = resolve<find_a_fn>("VerFindFileA");
    find_w = resolve<find_w_fn>("VerFindFileW");
    install_a = resolve<install_a_fn>("VerInstallFileA");
    install_w = resolve<install_w_fn>("VerInstallFileW");
    language_a = resolve<language_a_fn>("VerLanguageNameA");
    language_w = resolve<language_w_fn>("VerLanguageNameW");
    query_a = resolve<query_a_fn>("VerQueryValueA");
    query_w = resolve<query_w_fn>("VerQueryValueW");
    return true;
}
