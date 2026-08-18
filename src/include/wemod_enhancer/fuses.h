/*
 * fuses.h — Public API of the wemod_enhancer version.dll proxy.
 *
 * Installed next to the import library (lib/version.lib) so the
 * package doubles as a small SDK: custom patches and downstream tools
 * can reuse the Electron fuse manipulation routines.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef WEMOD_ENHANCER_FUSES_H
#define WEMOD_ENHANCER_FUSES_H

#include <windef.h> /* BOOL */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * disable_asar_integrity — locate Electron's fuse wire inside the
 * loaded image and flip the asar-integrity fuse to 'removed'.
 *
 * Returns TRUE if the fuse is disabled after the call — either flipped
 * here or already removed. Returns FALSE when the fuse wire was not
 * found, has an unexpected layout, or could not be made writable.
 */
BOOL disable_asar_integrity(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* WEMOD_ENHANCER_FUSES_H */
