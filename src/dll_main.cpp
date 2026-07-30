// SPDX-License-Identifier: MIT

#include "fuses.hpp"

#include <windows.h>

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) noexcept {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    DisableThreadLibraryCalls(module);
    (void)disable_asar_integrity();
    return TRUE;
}
