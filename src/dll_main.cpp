// SPDX-License-Identifier: MIT

#include <windows.h>

[[nodiscard]] auto disable_asar_integrity() noexcept -> bool;
[[nodiscard]] auto load_version_proxy() noexcept -> bool;

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) noexcept {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    DisableThreadLibraryCalls(module);
    if (!load_version_proxy()) {
        return FALSE;
    }

    (void)disable_asar_integrity();
    return TRUE;
}
