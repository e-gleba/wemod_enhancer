<div align="center">

# wemod_enhancer

**Cross-platform WeMod patcher: free Pro, no auto-updates, no telemetry, no mobile pairing.**

One command patches the WeMod client: Pro subscription active, auto-updates disabled, telemetry and mobile pairing gone. Works on Windows, Linux, and Steam Deck.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](./license.md)
[![ci](https://img.shields.io/github/actions/workflow/status/e-gleba/wemod_enhancer/cmake_multi_platform.yml?branch=main&label=ci)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/cmake_multi_platform.yml)
[![release](https://img.shields.io/github/v/release/e-gleba/wemod_enhancer)](https://github.com/e-gleba/wemod_enhancer/releases)
[![CMake](https://img.shields.io/badge/CMake-3.31+-064F8C?logo=cmake)](https://cmake.org)
[![Python](https://img.shields.io/badge/Python-3.11+-3776AB?logo=python&logoColor=white)](https://python.org)

</div>

---

## Features

| Feature | How |
| :------ | :-- |
| **Free Pro** | ASAR patch flips the subscription check |
| **No auto-updates** | Tiny C proxy DLL stubs the Squirrel `Update.exe` |
| **No telemetry** | ASAR patch strips segment.io / Sentry / analytics |
| **No mobile pairing** | `requestRemoteAuthCode()` → reject |
| **Fail-safe** | Automatic backup, all-or-nothing patching, one-command restore |

## GUI (optional)

Every package also ships a desktop GUI next to the CLI — same folder, use it or ignore it:

| OS | Artifact | Binary inside |
| :- | :------- | :------------ |
| Windows x64 | `wemod_enhancer-windows-msvc-amd64.zip` | `bin/wemod_enhancer_gui.exe` |
| Linux x64 | `wemod_enhancer-linux-amd64.tar.xz` | `bin/wemod_enhancer_gui.elf` |
| Linux ARM64 | `wemod_enhancer-linux-arm64.tar.xz` | `bin/wemod_enhancer_gui.elf` |

It finds the bundled patcher (`wemod_enhancer.py` + `version.dll`) next to itself, auto-detects the WeMod folder, streams the patch output live. Only requirement: **Python 3.11+** on PATH. **Report bug** opens a pre-filled GitHub issue with the log attached. The CLI in the same package works standalone.

## Linux / Steam Deck

WeMod is Windows-only. On Linux it runs through Wine/Proton — full setup guide: [docs/linux.md](docs/linux.md).

## Quick Start

```sh
# 1. Install WeMod, run it once, log in, close it

# 2. Patch
python wemod_enhancer.py patch

# 3. Later: restore the original
python wemod_enhancer.py restore
```

Requires Python 3.11+.

<details>
<summary><b>All commands</b></summary>

```text
patch     Backup → patch ASAR → block updates → strip telemetry
restore   Undo everything from the automatic backup
status    Show patch state, version, backup info
```

| Flag | Default | Purpose |
| :--- | :------ | :------ |
| `--install-dir <path>` | auto-detect | WeMod folder (overrides detection) |
| `--version-dll <path>` | auto-detect | Custom `version.dll` proxy |
| `--no-backup` | off | Skip backup (not recommended) |
| `--force` | off | Re-patch an already patched install |
| `--quiet` | off | Suppress info output |

</details>

## How It Works

```
┌─────────────────────────────────────────────────────┐
│  wemod_enhancer.py                                  │
│                                                     │
│  1. backup     app.asar → app.asar.bak              │
│  2. patch      flip Pro check, strip telemetry      │
│  3. block      drop version.dll proxy               │
│  4. verify     all-or-nothing, rollback on failure  │
└─────────────────────────────────────────────────────┘
```

**ASAR patch** — WeMod is an Electron app. Its logic lives in `resources/app.asar`, an uncompressed archive. The patcher finds the subscription/telemetry code and rewrites it in place. No binary disassembly needed — it's JavaScript.

**Update block** — WeMod uses Squirrel for auto-updates: it shells out to `Update.exe` next to the app. The patcher drops a 4 KB proxy DLL (`version.dll`) into the WeMod folder. Windows loads it instead of the system `version.dll` (DLL search order: app directory first). The proxy forwards every export to the real system DLL — except the ones Squirrel needs, which it stubs. WeMod can't self-update, everything else works.

<details>
<summary><b>Technical details</b></summary>

- **Proxy DLL**: C11, no dependencies beyond `kernel32`. Forwards 14 exports, stubs 1 (`GetFileVersionInfoSizeW` family used by Squirrel's update check).
- **ASAR format**: 4-byte header size + JSON header + flat file table. Patcher parses, patches, repacks — no external tools.
- **Detection**: registry (`HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\WeMod`), default `%LOCALAPPDATA%\WeMod`, common custom paths.
- **Backup**: `app.asar.bak` next to the original, plus a timestamped copy in `backups/`.

</details>

## Why Not Wand-Enhancer?

Ground-up rewrite of the original [Wand-Enhancer](https://github.com/k1tbyte/Wand-Enhancer) (archived, Windows-only, .NET/WPF).

| | wemod_enhancer | Wand-Enhancer |
| :-- | :-- | :-- |
| **Language** | C + Python (stdlib only) | C# / .NET / WPF |
| **Runtime deps** | None — Python stdlib + a 4 KB C DLL | .NET Framework 4.8 runtime |
| **Build deps** | CMake + compiler | CMake + Node.js + pnpm + VS 2022 + MSBuild + NuGet |
| **Build from source** | `cmake --workflow --preset windows_llvm_mingw_amd64_full` | Fork → GitHub Actions → download artifact |
| **Platform** | Steam Deck, Linux, Windows | Windows only |
| **Interface** | CLI — scriptable, automatable | WPF GUI — click-through wizard |
| **Binary size** | ~4 KB proxy DLL | Full .NET WPF application |
| **GUI app** | One click, zero terminal | — |

## Troubleshooting

| Problem | Fix |
| :------ | :-- |
| `WeMod installation not found` | Pass `--install-dir "C:\path\to\WeMod"` |
| Patch broke something | `python wemod_enhancer.py restore` |
| WeMod updated anyway | Re-run `patch` — the update replaced the proxy DLL |
| Antivirus flags `version.dll` | False positive on the proxy pattern — whitelist it or [build from source](#building) |

## Building

For builders, maintainers, and contributors — users should grab a prebuilt package from the [latest release](https://github.com/e-gleba/wemod_enhancer/releases/latest) instead.

Needs CMake 3.31+ and Ninja. On Linux the LLVM-MinGW toolchain is auto-downloaded. Every preset builds the full package (GUI + `version.dll` + `wemod_enhancer.py`) in RelWithDebInfo. Presets are named `os_compiler_arch`:

```sh
cmake --workflow --preset windows_llvm_mingw_amd64_full   # Linux → Windows package
cmake --workflow --preset windows_msvc_amd64_full         # Windows (MSVC) package
cmake --workflow --preset linux_gcc_amd64_full            # Linux (GCC) package
```

Native Linux builds need the SDL3 Wayland/X11 dev packages (build-time only) — see [cmake_multi_platform.yml](.github/workflows/cmake_multi_platform.yml) for the exact apt list; Fedora: `sudo dnf install libstdc++-static` for the static C++ runtime.

<div align="center">
<sub>MIT © 2026 Evgeniy Gleba · Not affiliated with WeMod.</sub>
</div>
