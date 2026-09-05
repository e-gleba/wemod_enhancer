<div align="center">

<img src="docs/logo.svg" alt="WeMod Enhancer" width="128">

# WeMod Enhancer

**The original WeMod app — Pro unlocked — on Steam Deck, Linux & Windows**

One command patches the WeMod client: Pro subscription active, auto-updates disabled, F12 DevTools, no mobile-pairing nag. Zero runtime dependencies — stdlib Python plus a 4 KB C proxy DLL.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](./license.md)
[![ci](https://img.shields.io/github/actions/workflow/status/e-gleba/wemod_enhancer/cmake_multi_platform.yml?branch=main&label=ci)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/cmake_multi_platform.yml)
[![release](https://img.shields.io/github/v/release/e-gleba/wemod_enhancer)](https://github.com/e-gleba/wemod_enhancer/releases)
[![CMake](https://img.shields.io/badge/CMake-3.31+-064F8C?logo=cmake)](https://cmake.org)
[![Python](https://img.shields.io/badge/Python-3.11+-3776AB?logo=python&logoColor=white)](https://python.org)
[![Platform](https://img.shields.io/badge/Platform-Steam_Deck_·_Linux_·_Windows-121212?logo=steam&logoColor=white)](#linux--steam-deck)

[![▶ run release](https://img.shields.io/badge/%E2%96%B6_run-release-2ea44f)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/publish_release.yml)
[![▶ run ci](https://img.shields.io/badge/%E2%96%B6_run-ci-2ea44f)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/cmake_multi_platform.yml)
[![▶ run renovate](https://img.shields.io/badge/%E2%96%B6_run-renovate-2ea44f)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/renovate.yml)

</div>

## One-liner

Close WeMod first, then paste one line. The script downloads the latest package, auto-detects the newest WeMod install, and patches it. `--install-dir` is optional everywhere.

Windows (PowerShell):

```powershell
irm https://raw.githubusercontent.com/e-gleba/wemod_enhancer/main/scripts/install.ps1 | iex
```

Linux / Steam Deck:

```sh
curl -fsSL https://raw.githubusercontent.com/e-gleba/wemod_enhancer/main/scripts/install.sh | bash
```

> No `python3` on Deck/Linux? It's preinstalled on SteamOS — otherwise install 3.11+ with your package manager. No `python` on Windows? The script installs Python 3.13 via `winget` automatically.

## What you get

| Feature | How |
| :------ | :-- |
| **Pro activation** | Intercepts `/v3/account` → injects `subscription: { period: "yearly", state: "active" }` |
| **ASAR integrity bypass** | 4 KB `version.dll` proxy flips Electron's fuse in-process via `VirtualProtect` |
| **F12 DevTools** | Hotkey hook on `browser-window-created` |
| **No auto-updates** | `ACTION_CHECK_FOR_UPDATE` → no-op — the patch survives until you say otherwise |
| **No mobile pairing** | `requestRemoteAuthCode()` → reject |
| **Fail-safe** | Automatic backup, all-or-nothing patching, one-command restore |

## GUI (optional)

One self-contained folder, no terminal. Grab the artifact for your OS from the [latest release](https://github.com/e-gleba/wemod_enhancer/releases/latest), unpack, run:

| OS | Artifact | Inside |
| :- | :------- | :----- |
| Windows x64 | `wemod_enhancer-windows-msvc-amd64.zip` | `bin/wemod_enhancer_gui.exe` + `bin/wemod_enhancer.py` + `bin/version.dll` |
| Linux x64 | `wemod_enhancer-linux-amd64.tar.xz` | `bin/wemod_enhancer_gui.elf` + `bin/wemod_enhancer.py` + `bin/version.dll` |
| Linux ARM64 | `wemod_enhancer-linux-arm64.tar.xz` | same layout, ARM build |

Everything ships inside the package — nothing is downloaded at runtime: the GUI finds the patcher (`wemod_enhancer.py` + `version.dll`) next to the executable, auto-detects the WeMod folder, streams the patch output live. Only requirement: **Python 3.11+** on PATH. **Report bug** opens a pre-filled GitHub issue with the log attached.

## Linux / Steam Deck

WeMod has no Linux build — [wemod-launcher](https://github.com/DeckCheatz/wemod-launcher) runs the official Windows client through Proton. WeMod Enhancer unlocks Pro on top of it.

Prefer the [one-liner](#one-liner) above. Manual equivalent:

```sh
# 1. Install the launcher
git clone https://github.com/DeckCheatz/wemod-launcher "$HOME/wemod-launcher"
chmod +x "$HOME/wemod-launcher/wemod"

# 2. Get WeMod Enhancer (prebuilt version.dll included — Python is all you need)
curl -LO https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-windows-llvm-mingw-amd64.tar.xz
mkdir -p wemod_enhancer && tar -xf wemod_enhancer-windows-llvm-mingw-amd64.tar.xz -C wemod_enhancer

# 3. Launch your game once through the launcher, then patch (auto-detects wemod_bin)
python3 wemod_enhancer/bin/wemod_enhancer.py patch
```

> **Important:** `wemod_bin` only appears **after** you launch a game through wemod-launcher at least once and log in to your WeMod account. The launcher downloads the WeMod client into `~/wemod-launcher/wemod_data/wemod_bin` on first run — patch only after that folder exists. See the [wemod-launcher tutorial](https://github.com/DeckCheatz/wemod-launcher#quick-guide) for the full setup (Proton, launch options, first login).

Steam launch options for the game:

```sh
WINEDLLOVERRIDES="version=n,b" "$HOME/wemod-launcher/wemod" %command%
```

> `version=n,b` forces native-first DLL loading — without it Wine uses its builtin `version.dll` and Electron rejects the patched ASAR.

Done — WeMod starts with the game, Pro active. Diagnostics, log locations, cleanup: [docs/linux.md](docs/linux.md).

## Windows

Prefer the [one-liner](#one-liner) above. Manual equivalent — open **PowerShell**, make sure WeMod is closed, then paste:

```powershell
cd $env:USERPROFILE\Downloads
Invoke-WebRequest -Uri "https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-windows-msvc-amd64.zip" -OutFile "wemod_enhancer.zip"
Expand-Archive wemod_enhancer.zip -DestinationPath wemod_enhancer -Force
cd wemod_enhancer
python bin\wemod_enhancer.py patch
```

> `python` not recognized? `winget install Python.Python.3.13`, then reopen PowerShell. The one-liner does this for you.

Explicit install dir (only when auto-detect picks the wrong copy):

```powershell
python bin\wemod_enhancer.py patch --install-dir "$env:LOCALAPPDATA\WeMod\app-10.2.3"
```

## CLI

`--install-dir` is optional on every command — the newest `app-*` holding `resources/app.asar` is used automatically.

```sh
python wemod_enhancer.py patch                 # patch auto-detected install
python wemod_enhancer.py patch --dry-run       # check only, change nothing
python wemod_enhancer.py patch --only devtools-f12 disable-updates
python wemod_enhancer.py status                # patch state, no changes
python wemod_enhancer.py doctor                # env + install health check
python wemod_enhancer.py list-patches          # what --only accepts
python wemod_enhancer.py restore               # revert from automatic backup
python wemod_enhancer.py patch --json          # machine-readable report
```

## Restore

Originals are backed up automatically. One command reverts everything:

```sh
# Linux
python3 bin/wemod_enhancer.py restore

# Windows (PowerShell)
python bin\wemod_enhancer.py restore
```

Pass `--install-dir` only when you patched an explicit folder.

## vs Wand-Enhancer

Ground-up rewrite of the original [Wand-Enhancer](https://github.com/k1tbyte/Wand-Enhancer) — same core job, zero runtime dependencies, fully cross-platform:

| | **WeMod Enhancer** | **Wand-Enhancer** |
| :-- | :------------------- | :------------------- |
| **Language** | C + Python (stdlib only) | C# / .NET / WPF |
| **Runtime deps** | None — Python stdlib + a 4 KB C DLL | .NET Framework 4.8 runtime |
| **Build deps** | CMake + compiler | CMake + Node.js + pnpm + VS 2022 + MSBuild + NuGet |
| **Build from source** | `cmake --workflow --preset windows_llvm_mingw_amd64_full` | Fork → GitHub Actions → download artifact |
| **Platform** | Steam Deck, Linux, Windows | Windows only |
| **Interface** | CLI + optional GUI — scriptable, one-liner installs | WPF GUI — click-through wizard |
| **Binary size** | ~4 KB proxy DLL | Full .NET WPF application |
| **ASAR integrity bypass** | In-process fuse flip via `VirtualProtect` | Same approach (shared heritage) |
| **Pro activation** | Intercepts `/v3/account` responses (+ reducer + language + brand variants) | Same core + reducer variant |
| **DevTools** | F12 hotkey hook | Same |
| **Disable updates** | `ACTION_CHECK_FOR_UPDATE` → no-op | Same |
| **Disable mobile pairing** | `requestRemoteAuthCode()` → reject | Same |
| **Remote web panel** | Not included | Built-in LAN HTTP/WebSocket server |
| **Custom script injection** | Not included (use `--only` to pick patches) | Bundled `.js` injection at patch time |
| **Auto-detect install** | Yes — newest `app-*`, launcher `wemod_bin`, one-liners | Manual folder pick in the wizard |
| **Dry-run / status / doctor** | Yes — `patch --dry-run`, `status`, `doctor --json` | Per-patch toggles in the GUI |
| **Fail-safe** | Fails closed on mismatched patches, idempotent re-runs | — |
| **Backup & restore** | Automatic, one-command restore | — |
| **Tests** | `pytest tests/` — regex sanity + ASAR round-trip | Web + desktop patch-state checks in `build.cmd` |
| **License** | MIT | Apache-2.0 |

> WeMod Enhancer focuses on the core patching pipeline with the smallest possible footprint. If you need the Remote Web Panel or custom script injection on Windows, Wand-Enhancer remains a solid choice.

## Build from source

For builders, maintainers, and contributors — users should grab a prebuilt package from the [latest release](https://github.com/e-gleba/wemod_enhancer/releases/latest) instead.

Needs CMake 3.31+ and Ninja. On Linux the LLVM-MinGW toolchain is auto-downloaded.

```sh
cmake --workflow --preset windows_llvm_mingw_amd64_full   # Linux → Windows package
cmake --workflow --preset windows_msvc_amd64_full         # Windows (MSVC) package
```

GUI builds on Linux hosts need SDL3 Wayland/X11 dev packages (build-time only) — see [cmake_multi_platform.yml](.github/workflows/cmake_multi_platform.yml) for the exact apt list; Fedora: `sudo dnf install libstdc++-static` for the static C++ runtime.

<div align="center">
<sub>MIT © 2026 Evgeniy Gleba · Not affiliated with WeMod.</sub>
</div>
