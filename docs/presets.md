# Presets, Platforms & Cross-Compilation

**The definitive guide to this template's build system. One of the most comprehensive preset collections in open-source C++ templates.**

CMake 3.21+ **Presets** are the primary interface. They eliminate "works on my machine" by encoding compilers, toolchains, generators, cache variables, and workflows in `CMakePresets.json`.

This document merges and expands the previous `presets.md` and `platforms.md` into a single professional reference. It explains **how presets make cross-compilation transparent**, documents every current configuration, and outlines the roadmap for Apple platforms.

## Core Philosophy

- **Discoverability**: `cmake --list-presets` shows everything.
- **Inheritance**: Base presets (`gcc-base`, `android-base`) reduce duplication.
- **Multi-Config with Ninja**: Fast iteration; separate debug/release without reconfigure.
- **Workflows**: `cmake --workflow --preset=xxx-full` runs configure → build → test → package in one command (where applicable).
- **Reproducibility**: Docker images, pinned NDK, llvm-mingw in PATH, explicit cache vars.
- **Extensibility**: Easy to add your own presets that inherit from these.

**Cross-compilation is first-class** — not an afterthought. Android NDK presets, llvm-mingw Windows cross presets, and planned Xcode presets demonstrate production-grade toolchain management.

## Quick Start Reference

```bash
# 1. Native Linux (recommended starting point)
cmake --preset=gcc
cmake --build --preset=gcc-release
ctest --preset=gcc-release
cpack --preset=gcc-package

# 2. Full CI-like pipeline
cmake --workflow --preset=gcc-full

# 3. Android (requires ANDROID_NDK_HOME)
export ANDROID_NDK_HOME=~/Android/Sdk/ndk/28.0.12433566
cmake --workflow --preset=android-arm64-full

# 4. Linux → Windows cross (requires llvm-mingw on PATH)
cmake --workflow --preset=llvm-mingw-x86_64-full
```

See `CMakePresets.json` for the full list (10+ configure presets + derived build/test/package/workflow variants).

## Preset Overview

### Native Development

| Preset Family | Compiler | Generator | Key Features | Test Support | Package Format |
|---------------|----------|-----------|--------------|--------------|----------------|
| `gcc` / `gcc-*` | GCC 13+ | Ninja Multi-Config | Baseline, excellent warnings | Full (`ctest`) | `.tar.gz` |
| `clang` / `clang-*` | Clang 16+ | Ninja Multi-Config | Modern diagnostics, IWYU ready | Full | `.tar.gz` |
| `msvc` / `msvc-*` | MSVC 2022 | Visual Studio 17 2022 | Native Windows development | Full | `.zip` |

### Android NDK (API 24+)

| Preset | ABI | Architecture | Notes |
|--------|-----|--------------|-------|
| `android-arm64` | `arm64-v8a` | AArch64 | Primary mobile target |
| `android-arm32` | `armeabi-v7a` | ARMv7 | Legacy support |
| `android-x64` | `x86_64` | x86_64 | Emulator |
| `android-x86` | `x86` | x86 | Emulator |

**Setup**: Set `ANDROID_NDK_HOME`. Presets automatically configure `CMAKE_TOOLCHAIN_FILE` to NDK's `android.toolchain.cmake`, set `CMAKE_ANDROID_API`, and disable tests (Android instrumentation tests planned for future).

### Windows Cross-Compilation (from Linux)

Uses **[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)** — modern, LLVM-based, excellent C++ support.

| Preset | Target Triplet | Notes |
|--------|----------------|-------|
| `llvm-mingw-x86_64` | `x86_64-w64-mingw32` | Primary (64-bit Windows) |
| `llvm-mingw-i686` | `i686-w64-mingw32` | 32-bit Windows |
| `llvm-mingw-aarch64` | `aarch64-w64-mingw32` | Windows on ARM |

**Setup**: Install llvm-mingw and ensure `x86_64-w64-mingw32-gcc` etc. are on `$PATH`. Presets set appropriate `CMAKE_C(XX)_COMPILER`, sysroot, and package as `.tar.xz`.

**Why llvm-mingw over MSVC cross?** Smaller toolchain, no Visual Studio dependency on Linux host, better integration with Ninja.

### Planned: Apple Platforms (macOS, iOS, Xcode)

**See [#20](https://github.com/e-gleba/cmake_template/issues/20) — "feat(platform): add macOS and iOS presets with Xcode generator support"**

This is a high-priority roadmap item to make the template truly universal.

**Planned Presets (subject to refinement):**
- `macos-xcode` / `macos-xcode-universal`: Apple Silicon + Intel universal binaries using `Xcode` generator, `CMAKE_OSX_DEPLOYMENT_TARGET=14.0`
- `ios-xcode-device`: `CMAKE_SYSTEM_NAME=iOS`, `OS64` arm64
- `ios-xcode-simulator`: Simulator SDK with `SIMULATORARM64`

**Key Features to Implement:**
- Proper `Xcode` generator support for `.xcodeproj`, schemes, code signing (`CODE_SIGN_IDENTITY`, `DEVELOPMENT_TEAM`)
- Universal binaries (`CMAKE_OSX_ARCHITECTURES=arm64;x86_64`)
- Integration with GitHub `macos-14` / `macos-15` runners in CI
- Documentation for common pitfalls (entitlements, provisioning profiles, notarization with `notarytool`)
- Optional support for Catalyst, tvOS, visionOS in future
- Synergy with existing CPack for `.dmg` or framework packaging

**Why Xcode generator?** It is the only generator that fully supports Apple's build system, asset catalogs, Storyboards (when mixed with Swift), Instruments profiling, and App Store submission workflows. Ninja on macOS is useful but secondary.

This addition will close a major gap compared to specialized JUCE or game templates while maintaining the generic, engineering-focused nature of this template.

Contributions toward #20 are highly encouraged (help wanted label).

## Advanced Usage & Extension

### Workflows (CMake 3.25+)

Workflow presets chain multiple steps. Current full workflows skip tests for cross targets (sensible default; runtime testing requires emulator or target hardware).

Example output of a full workflow includes CPack artifacts ready for distribution.

### Adding Your Own Presets

1. Copy an existing similar preset in `CMakePresets.json`.
2. Inherit from base using `"inherits": ["gcc-base"]`.
3. Add platform-specific `cacheVariables` (e.g. `CMAKE_SYSTEM_NAME`, `CMAKE_OSX_SYSROOT`).
4. Update this document and the README comparison table.
5. Test on relevant hardware/CI.
6. For new platforms, consider adding a Docker variant in `docker/`.

**Best Practice**: Use `condition` fields to hide macOS-only presets on Linux hosts.

### Troubleshooting Common Issues

- **NDK not found**: Explicitly set `ANDROID_NDK_HOME` or use Android Studio's NDK.
- **llvm-mingw**: Verify with `x86_64-w64-mingw32-g++ --version`.
- **Permission errors on Android**: API level and ABI must match target device/emulator.
- **CPack package names**: Controlled via `CPACK_*` variables in presets or CMakeLists.
- **IDE integration**: Presets generate `compile_commands.json`; most IDEs (CLion, VS Code, Qt Creator) auto-detect.

## Comparison to Other Templates

This template's preset matrix is more extensive than most for cross-platform C++ (Android + Windows cross out-of-box). The planned Apple support will make it one of the broadest.

See the main README for detailed feature comparison.

## Further Reading

- [Architecture](architecture.md)
- [Docker Guide](docker.md)
- [References](references.md)
- [Contributing](contributing.md) — especially the section on adding new platforms

---

**This documentation is intentionally professional-grade** to serve both individual developers and consulting clients evaluating the template for team adoption. Feedback on clarity or additional examples is welcome.
