// SPDX-License-Identifier: MIT

#include "proxy.hpp"

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
