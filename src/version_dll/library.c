/*
 * library.c — version.dll proxy DLL with ASAR integrity bypass on load.
 *
 * Based on concepts from Wand-Enhancer (https://github.com/k1tbyte/Wand-Enhancer)
 * Copyright 2024 k1tbyte — Licensed under Apache License 2.0
 *
 * Modified: Rewritten in C by Evgeniy Gleba, 2026.
 *
 * SPDX-License-Identifier: MIT
 */

/* NOVERSION suppresses version function declarations in the MSVC SDK.
   MinGW-w64's winver.h does NOT honour NOVERSION, so we must also
   prevent winver.h from being included when building with MinGW.
   We provide our own forwarders via VERSION_EXPORTS below. */
#define NOVERSION
#ifdef __MINGW32__
  /* MinGW-w64's winver.h ignores NOVERSION and declares Ver* with
     non-const params (LPSTR).  Guard it out so our prototypes win. */
  #define VER_H 1
  #define _WINVER_H 1
#endif
#include <windows.h>

extern BOOL disable_asar_integrity(void);
static HMODULE original_version;

/* MinGW-w64 declares Ver* params as non-const (LPSTR/LPWSTR).
   The MSVC SDK declares them const-correct (LPCSTR/LPCWSTR).
   Use the matching type for each toolchain. */
#ifdef __MINGW32__
  #define V_STR_A  LPSTR
  #define V_STR_W  LPWSTR
#else
  #define V_STR_A  LPCSTR
  #define V_STR_W  LPCWSTR
#endif

#define VERSION_EXPORTS(X) \
 X(GetFileVersionInfoA,BOOL,FALSE,(LPCSTR a,DWORD b,DWORD c,LPVOID d),(a,b,c,d)) \
 X(GetFileVersionInfoExA,BOOL,FALSE,(DWORD a,LPCSTR b,DWORD c,DWORD d,LPVOID e),(a,b,c,d,e)) \
 X(GetFileVersionInfoExW,BOOL,FALSE,(DWORD a,LPCWSTR b,DWORD c,DWORD d,LPVOID e),(a,b,c,d,e)) \
 X(GetFileVersionInfoSizeA,DWORD,0,(LPCSTR a,LPDWORD b),(a,b)) \
 X(GetFileVersionInfoSizeExA,DWORD,0,(DWORD a,LPCSTR b,LPDWORD c),(a,b,c)) \
 X(GetFileVersionInfoSizeExW,DWORD,0,(DWORD a,LPCWSTR b,LPDWORD c),(a,b,c)) \
 X(GetFileVersionInfoSizeW,DWORD,0,(LPCWSTR a,LPDWORD b),(a,b)) \
 X(GetFileVersionInfoW,BOOL,FALSE,(LPCWSTR a,DWORD b,DWORD c,LPVOID d),(a,b,c,d)) \
 X(VerFindFileA,DWORD,0,(DWORD a,V_STR_A b,V_STR_A c,V_STR_A d,LPSTR e,PUINT f,LPSTR g,PUINT h),(a,b,c,d,e,f,g,h)) \
 X(VerFindFileW,DWORD,0,(DWORD a,V_STR_W b,V_STR_W c,V_STR_W d,LPWSTR e,PUINT f,LPWSTR g,PUINT h),(a,b,c,d,e,f,g,h)) \
 X(VerInstallFileA,DWORD,0,(DWORD a,V_STR_A b,V_STR_A c,V_STR_A d,V_STR_A e,V_STR_A f,LPSTR g,PUINT h),(a,b,c,d,e,f,g,h)) \
 X(VerInstallFileW,DWORD,0,(DWORD a,V_STR_W b,V_STR_W c,V_STR_W d,V_STR_W e,V_STR_W f,LPWSTR g,PUINT h),(a,b,c,d,e,f,g,h)) \
 X(VerLanguageNameA,DWORD,0,(DWORD a,LPSTR b,DWORD c),(a,b,c)) \
 X(VerLanguageNameW,DWORD,0,(DWORD a,LPWSTR b,DWORD c),(a,b,c)) \
 X(VerQueryValueA,BOOL,FALSE,(LPCVOID a,LPCSTR b,LPVOID *c,PUINT d),(a,b,c,d)) \
 X(VerQueryValueW,BOOL,FALSE,(LPCVOID a,LPCWSTR b,LPVOID *c,PUINT d),(a,b,c,d))

#define DECLARE(name,type,fallback,params,args) \
 typedef type (WINAPI *name##_fn) params; static name##_fn p_##name; \
 type WINAPI name params { if (!p_##name) { SetLastError(ERROR_PROC_NOT_FOUND); return fallback; } return p_##name args; }
VERSION_EXPORTS(DECLARE)

BOOL WINAPI GetFileVersionInfoByHandle(void) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE; }

static BOOL load_original(void) {
    WCHAR path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    static const WCHAR suffix[] = L"\\version.dll";
    if (!n || n >= MAX_PATH || n + ARRAYSIZE(suffix) > MAX_PATH) return FALSE;
    CopyMemory(path + n, suffix, sizeof(suffix));
    original_version = LoadLibraryW(path);
    if (!original_version) return FALSE;
#define LOAD(name,type,fallback,params,args) p_##name = (name##_fn)GetProcAddress(original_version, #name);
    VERSION_EXPORTS(LOAD)
    return TRUE;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (!load_original()) return FALSE;
        disable_asar_integrity();
    }
    return TRUE;
}
