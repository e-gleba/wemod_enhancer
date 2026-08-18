<div align="center">

<img src="docs/logo.svg" alt="WeMod Enhancer" width="128">

# WeMod Enhancer

**The original WeMod app — Pro unlocked — on Steam Deck, Linux & Windows**

One command patches the WeMod client: Pro subscription active, auto-updates disabled, F12 DevTools, no mobile-pairing nag. Zero runtime dependencies — stdlib Python plus a 4 KB C proxy DLL.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](./license.md)
[![ci](https://img.shields.io/github/actions/workflow/status/e-gleba/wemod_enhancer/cmake_multi_platform.yml?branch=main&label=ci)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/cmake_multi_platform.yml)
[![gui ci](https://img.shields.io/github/actions/workflow/status/e-gleba/wemod_enhancer/gui.yml?branch=main&label=gui%20ci)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/gui.yml)
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

## GUI (optional)

Prefer clicking over typing? `wemod_enhancer_gui` is a small Dear ImGui app (SDL3, statically linked) that drives the same patcher — a pure downloader/automator, fully separate from the DLL build:

- **Only input needed: the WeMod folder** — auto-detected at startup (newest `%LOCALAPPDATA%\WeMod\app-*` on Windows, `~/wemod-launcher/wemod_data/wemod_bin` on Linux). When found, the path is already filled in: just press Patch, no Browse dialog needed.
- **Nothing else to download, ever** — the GUI pulls the latest release (`wemod_enhancer.py` + `version.dll`) itself: `curl` on Linux, PowerShell on Windows. No local files required; the exe is fully portable (no hardcoded paths), move it between PCs as-is.
- **Setup / diagnostics** panel shows the detected Python (version + location) and the patcher, and on Linux offers to clone [wemod-launcher](https://github.com/DeckCheatz/wemod-launcher) into your home dir — the same step as the tutorial below.
- Patch / Restore stream output live; **Copy output** grabs the environment info plus the whole log, and **Report bug** opens a pre-filled GitHub issue with the same data attached as markdown — a one-click bug report.

The GUI presets are mutually exclusive with the plain-DLL ones (a GUI build never compiles `version.dll` — the GUI downloads it at runtime), ship as their own package (`wemod_enhancer_gui-<version>-<os>-<arch>.zip` / `.tar.xz`), and are built **RelWithDebInfo** so crashes stay debuggable (PDB next to the exe on Windows, symbols in the `.elf` on Linux). The GUI has its own CI workflow ([gui.yml](.github/workflows/gui.yml)) with a compiler × arch matrix:

| Preset | Host | Compiler | Artifact |
| :----- | :--- | :------- | :------- |
| `gui-windows-msvc-amd64-full` | Windows x86-64 | MSVC | `wemod_enhancer_gui.exe` + `.pdb` |
| `gui-windows-clang-amd64-full` | Windows x86-64 | Clang (MSVC ABI, lld-link) | `wemod_enhancer_gui.exe` + `.pdb` |
| `gui-windows-msvc-arm64-full` | Windows ARM64 (ARM64 host) | MSVC | `wemod_enhancer_gui.exe` + `.pdb` |
| `gui-windows-llvm-mingw-amd64-full` | Linux → Windows x86-64 | LLVM-MinGW | `wemod_enhancer_gui.exe` |
| `gui-linux-gcc-amd64-full` | Linux x86-64 | GCC | `wemod_enhancer_gui.elf` |
| `gui-linux-clang-amd64-full` | Linux x86-64 | Clang | `wemod_enhancer_gui.elf` |
| `gui-linux-gcc-arm64-full` | Linux ARM64 | GCC | `wemod_enhancer_gui.elf` |

```sh
cmake --workflow --preset gui-windows-msvc-amd64-full   # build/gui-windows-msvc-amd64/RelWithDebInfo/wemod_enhancer_gui.exe
cmake --workflow --preset gui-linux-gcc-amd64-full      # build/gui-linux-gcc-amd64/RelWithDebInfo/wemod_enhancer_gui.elf
```

Linux host builds need SDL3 Wayland/X11 dev packages (build-time only), e.g. `sudo apt-get install libwayland-dev libxkbcommon-dev libdbus-1-dev libibus-1.0-dev libdecor-0-dev`; Fedora: `sudo dnf install libstdc++-static` for the static C++ runtime.

## Linux / Steam Deck

WeMod has no Linux build — [wemod-launcher](https://github.com/DeckCheatz/wemod-launcher) runs the official Windows client through Proton. WeMod Enhancer unlocks Pro on top of it.

```sh
# 1. Install the launcher
git clone https://github.com/DeckCheatz/wemod-launcher "$HOME/wemod-launcher"
chmod +x "$HOME/wemod-launcher/wemod"

# 2. Get WeMod Enhancer (prebuilt version.dll included — Python is all you need)
curl -LO https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-windows-llvm-mingw-amd64.tar.xz
mkdir -p wemod_enhancer && tar -xf wemod_enhancer-windows-llvm-mingw-amd64.tar.xz -C wemod_enhancer
cd wemod_enhancer

# 3. Launch your game once through the launcher, then patch the WeMod install
python3 bin/wemod_enhancer.py patch --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
```

> No `python3`? It's preinstalled on SteamOS and most distros — otherwise install it with your package manager (3.11+).

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

> `python` not recognized? `winget install Python.Python.3.13`, then reopen PowerShell. Don't want Python staying on your system after patching? Remove it the same way: `winget uninstall Python.Python.3.13`.

## Restore

Originals are backed up automatically. One command reverts everything:

```sh
# Linux
python3 bin/wemod_enhancer.py restore --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"

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

## vs Wand-Enhancer

Ground-up rewrite of the original [Wand-Enhancer](https://github.com/k1tbyte/Wand-Enhancer) — same core job, zero runtime dependencies, fully cross-platform:

| | **WeMod Enhancer** | **Wand-Enhancer** |
| :-- | :------------------- | :------------------- |
| **Language** | C + Python (stdlib only) | C# / .NET / WPF |
| **Runtime deps** | None — Python stdlib + a 4 KB C DLL | .NET Framework 4.8 runtime |
| **Build deps** | CMake + compiler | CMake + Node.js + pnpm + VS 2022 + MSBuild + NuGet |
| **Build from source** | `python3 tools/wemod_enhancer.py build-dll` | Fork → GitHub Actions → download artifact |
| **Platform** | Steam Deck, Linux, Windows | Windows only |
| **Interface** | CLI — scriptable, automatable | WPF GUI — click-through wizard |
| **Binary size** | ~4 KB proxy DLL | Full .NET WPF application |
| **ASAR integrity bypass** | In-process fuse flip via `VirtualProtect` | Same approach (shared heritage) |
| **Pro activation** | Intercepts `/v3/account` responses | Same |
| **DevTools** | F12 hotkey hook | Not available |
| **Disable updates** | `ACTION_CHECK_FOR_UPDATE` → no-op | Not available |
| **Disable mobile pairing** | `requestRemoteAuthCode()` → reject | Not available |
| **Remote web panel** | Not included | Built-in LAN HTTP/WebSocket server |
| **Custom script injection** | Not included | Bundled `.js` injection at patch time |
| **Fail-safe** | Fails closed on mismatched patches | — |
| **Backup & restore** | Automatic, one-command restore | — |
| **License** | MIT | Apache-2.0 |

> WeMod Enhancer focuses on the core patching pipeline with the smallest possible footprint. If you need the Remote Web Panel or custom script injection on Windows, Wand-Enhancer remains a solid choice.

<div align="center">
<sub>MIT © 2026 Evgeniy Gleba · Not affiliated with WeMod.</sub>
</div>
