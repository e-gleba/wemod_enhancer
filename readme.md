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

## What you get

| Feature | How |
| :------ | :-- |
| **Pro activation** | Intercepts `/v3/account` → injects `subscription: { period: "yearly", state: "active" }` |
| **ASAR integrity bypass** | 4 KB `version.dll` proxy flips Electron's fuse in-process via `VirtualProtect` |
| **F12 DevTools** | Hotkey hook on `browser-window-created` |
| **No auto-updates** | `ACTION_CHECK_FOR_UPDATE` → no-op — the patch survives until you say otherwise |
| **No mobile pairing** | `requestRemoteAuthCode()` → reject |
| **Fail-safe** | Automatic backup, all-or-nothing patching, one-command restore |

## Linux / Steam Deck

WeMod has no Linux build — [wemod-launcher](https://github.com/DeckCheatz/wemod-launcher) runs the official Windows client through Proton. WeMod Enhancer unlocks Pro on top of it.

```sh
# 1. Install the launcher
git clone https://github.com/DeckCheatz/wemod-launcher "$HOME/wemod-launcher"
chmod +x "$HOME/wemod-launcher/wemod"

# 2. Get WeMod Enhancer (prebuilt version.dll included — Python is all you need)
curl -LO https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-windows-llvm-mingw-amd64.tar.xz
tar -xf wemod_enhancer-windows-llvm-mingw-amd64.tar.xz

# 3. Launch your game once through the launcher, then patch the WeMod install
python3 bin/wemod_enhancer.py patch --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
```

Steam launch options for the game:

```sh
WINEDLLOVERRIDES="version=n,b" "$HOME/wemod-launcher/wemod" %command%
```

> `version=n,b` forces native-first DLL loading — without it Wine uses its builtin `version.dll` and Electron rejects the patched ASAR.

Done — WeMod starts with the game, Pro active. Diagnostics, log locations, cleanup: [docs/linux.md](docs/linux.md).

## Windows

Open **PowerShell** (Start → type `powershell` → Enter), make sure WeMod is closed, then paste:

```powershell
# 1. Download + extract WeMod Enhancer (prebuilt version.dll included)
cd $env:USERPROFILE\Downloads
Invoke-WebRequest -Uri "https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-windows-msvc-amd64.zip" -OutFile "wemod_enhancer.zip"
Expand-Archive wemod_enhancer.zip -DestinationPath wemod_enhancer -Force
cd wemod_enhancer

# 2. Auto-detect the newest WeMod install (the app-* folder with resources\app.asar) and patch it
$wemod = Get-ChildItem "$env:LOCALAPPDATA\WeMod\app-*" -Directory |
         Where-Object { Test-Path "$_\resources\app.asar" } |
         Sort-Object { [version]($_.Name -replace '^app-','') } -Descending |
         Select-Object -First 1
python bin\wemod_enhancer.py patch --install-dir $wemod.FullName
```

> No Python yet? `winget install Python.Python.3.13`, then reopen PowerShell.

## Restore

Originals are backed up automatically. One command reverts everything:

```sh
# Linux
python3 bin/wemod_enhancer.py restore --install-dir "<same install dir>"

# Windows (PowerShell, same session as above)
python bin\wemod_enhancer.py restore --install-dir $wemod.FullName
```

## Build from source

Needs CMake 3.31+ and Ninja. On Linux the LLVM-MinGW toolchain is auto-downloaded.

```sh
python3 tools/wemod_enhancer.py build-dll           # just the proxy DLL
cmake --workflow --preset llvm-mingw-x86_64-full    # Linux → Windows package
cmake --workflow --preset msvc-full                 # Windows (MSVC) package
```

## GitHub Actions

| Action | How to run | What it does |
| :----- | :--------- | :----------- |
| **Release** | [Actions → publish_release](https://github.com/e-gleba/wemod_enhancer/actions/workflows/publish_release.yml) → *Run workflow* → enter version (e.g. `v1.0.0`) | Builds MSVC + Clang + LLVM-MinGW archives, tags the commit, publishes a GitHub Release with generated notes |
| **CI** | Automatic on push/PR — manual: [Actions → cmake_multiplatform_workflow](https://github.com/e-gleba/wemod_enhancer/actions/workflows/cmake_multi_platform.yml) → *Run workflow* | Builds and packages every preset |
| **Renovate** | Automatic weekly — manual: [Actions → Renovate](https://github.com/e-gleba/wemod_enhancer/actions/workflows/renovate.yml) → *Run workflow* (`dryRun` optional) | Dependency-update PRs |

---

Ground-up rewrite of [Wand-Enhancer](https://github.com/k1tbyte/Wand-Enhancer) (C#/.NET WPF, Windows-only) — same core job, zero runtime dependencies, fully cross-platform.

<div align="center">
<sub>MIT © 2026 Evgeniy Gleba · Not affiliated with WeMod.</sub>
</div>
