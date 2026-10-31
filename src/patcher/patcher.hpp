#pragma once

#include <filesystem>

namespace wemod::patcher
{
/// Patch the Wand/WeMod client at `install_dir`.
///
/// Mirrors scripts/wemod_enhancer.py `patch()`: backs up
/// resources/app.asar, extracts it, applies the JS bundle patches,
/// repacks with fresh SHA-256 integrity, removes the legacy
/// version.dll proxy leftovers, and flips the Electron ASAR-integrity
/// fuse to `r` (removed).
///
/// Throws std::runtime_error / std::filesystem::filesystem_error on
/// failure. Strong guarantee for app.asar: the backup is restored on
/// error. Idempotent: re-running resets app.asar from the backup first.
void patch(const std::filesystem::path& install_dir);

/// Restore the client at `install_dir` from resources/app.asar.backup.
///
/// Mirrors scripts/wemod_enhancer.py `restore()`: copies the backup
/// over app.asar, cleans the legacy version.dll proxy, and restores
/// the Electron fuse from its backup (or legacy sidecar).
///
/// Throws if the backup is missing.
void unpatch(const std::filesystem::path& install_dir);
} // namespace wemod::patcher
