<div align="center">

<img src="docs/logo.svg" alt="WeMod Enhancer logo" width="128">

# WeMod Enhancer

**Unlock WeMod Pro on Steam Deck, Linux, and Windows.**

One command patches the WeMod client to enable Pro, disable auto-updates, enable the F12 DevTools, and remove the mobile-pairing prompt. The only requirement is [Python](https://www.python.org/downloads/).

[![CI](https://img.shields.io/github/actions/workflow/status/e-gleba/wemod_enhancer/cmake_multi_platform.yml?branch=main&label=CI)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/cmake_multi_platform.yml)
[![Release](https://img.shields.io/github/v/release/e-gleba/wemod_enhancer)](https://github.com/e-gleba/wemod_enhancer/releases)
[![Platform](https://img.shields.io/badge/Platform-Steam_Deck_·_Linux_·_Windows-121212?logo=steam&logoColor=white)](#linux--steam-deck-cli)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

[![Run release](https://img.shields.io/badge/%E2%96%B6_Run-Release-2ea44f)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/publish_release.yml)
[![Run CI](https://img.shields.io/badge/%E2%96%B6_Run-CI-2ea44f)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/cmake_multi_platform.yml)
[![Run Renovate](https://img.shields.io/badge/%E2%96%B6_Run-Renovate-2ea44f)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/renovate.yml)

</div>

## Contents

- [GUI](#gui)
- [Linux / Steam Deck (CLI)](#linux--steam-deck-cli)
- [Windows (CLI)](#windows-cli)
- [Restore](#restore)
- [Comparison with Wand-Enhancer](#comparison-with-wand-enhancer)
- [Build from source](#build-from-source)

> [!WARNING]
> Sign in to your WeMod account before patching. The patcher does not work without an authorized WeMod session.
> This requirement is repeated in each platform section to prevent confusion.

## GUI

![WeMod Enhancer GUI](docs/Screenshot_20260930_224227.png)

The GUI ships as a self-contained folder and requires no terminal. Download the artifact for your OS from the [latest release](https://github.com/e-gleba/wemod_enhancer/releases/latest), extract it, and run the executable. Make sure to log in to your WeMod account before patching.

| OS          | Artifact                                           |
| ----------- | -------------------------------------------------- |
| Windows x64 | [`wemod_enhancer-windows-msvc-amd64.zip`][win-x64] |
| Linux x64   | [`wemod_enhancer-linux-amd64.tar.xz`][linux-x64]   |
| Linux ARM64 | [`wemod_enhancer-linux-arm64.tar.xz`][linux-arm64] |

[win-x64]: https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-windows-msvc-amd64.zip
[linux-x64]: https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-linux-amd64.tar.xz
[linux-arm64]: https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-linux-arm64.tar.xz

All components are included in the package, and nothing is downloaded at runtime. The GUI locates `wemod_enhancer.py` next to the executable, detects the WeMod installation folder automatically, and streams patch output live. **Report bug** opens a pre-filled GitHub issue with the log attached.

**Requirements:** [Python 3.11+](https://www.python.org/downloads/) available on `PATH`.

## Linux / Steam Deck (CLI)

WeMod does not provide a native Linux build. [wemod-launcher](https://github.com/DeckCheatz/wemod-launcher) runs the official Windows client through Proton, and WeMod Enhancer then unlocks Pro on top of it.

1. **Install the launcher**

   ```sh
   git clone https://github.com/DeckCheatz/wemod-launcher "$HOME/wemod-launcher"
   chmod +x "$HOME/wemod-launcher/wemod"
   ```

   Follow the [wemod-launcher quick guide](https://github.com/DeckCheatz/wemod-launcher#quick-guide) for setup.

   > **Note:** ProtonQT is not required. You can use vanilla Proton if you encounter problems with it.

2. **Set the Steam launch options** for the game (better do it on some offline game to avoid anti-cheat problems):

   ```sh
   WEMOD_LOG="$HOME/wemod-launcher/wemod.log" "$HOME/wemod-launcher/wemod" %command%
   ```

3. **Launch the game and log in to WeMod.**

4. **Download WeMod Enhancer**

   ```sh
   curl -LO https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-windows-llvm-mingw-amd64.tar.xz
   mkdir -p wemod_enhancer && tar -xf wemod_enhancer-windows-llvm-mingw-amd64.tar.xz -C wemod_enhancer
   cd wemod_enhancer
   ```

5. **Launch the game once through the launcher, then patch the WeMod installation**

   ```sh
   python3 bin/wemod_enhancer.py patch --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
   ```

   To launch the GUI instead, run:

   ```sh
   ./bin/wemod_enhancer_gui.elf
   ```

   > **Important:** The `wemod_bin` directory is created only after you launch a game through wemod-launcher at least once and log in to your WeMod account. The launcher downloads the WeMod client to `~/wemod-launcher/wemod_data/wemod_bin` on first run. Patch only after this directory exists.

   > **Note:** If `python3` is not installed, install Python 3.11+ with your package manager. It is preinstalled on SteamOS and most major distributions.

## Windows (CLI)

1. Install WeMod and log in.
2. Close WeMod.
3. Open **PowerShell** (Start → type `powershell` → Enter).
4. Download and extract WeMod Enhancer:

   ```powershell
   cd $env:USERPROFILE\Downloads
   Invoke-WebRequest -Uri "https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-windows-msvc-amd64.zip" -OutFile "wemod_enhancer.zip"
   Expand-Archive wemod_enhancer.zip -DestinationPath wemod_enhancer -Force
   cd wemod_enhancer
   ```

5. Detect the newest WeMod installation (the `app-*` folder that contains `resources\app.asar`) and patch it:

   ```powershell
   $wemod = Get-ChildItem "$env:LOCALAPPDATA\WeMod\app-*" -Directory |
            Where-Object { Test-Path "$_\resources\app.asar" } |
            Sort-Object { [version]($_.Name -replace '^app-','') } -Descending |
            Select-Object -First 1
   python bin\wemod_enhancer.py patch --install-dir $wemod.FullName
   ```

   To launch the GUI instead, run:

   ```powershell
   .\bin\wemod_enhancer_gui.exe
   ```

> **Note:** If `python` is not recognized, install [Python 3.13](https://www.python.org/downloads/) (`winget install Python.Python.3.13`) and reopen PowerShell. To remove Python after patching, run `winget uninstall Python.Python.3.13`.

## Restore

Original files are backed up automatically. A single command reverts all changes.

On Linux:

```sh
python3 bin/wemod_enhancer.py restore --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
```

On Windows, run this in PowerShell:

```powershell
$wemod = Get-ChildItem "$env:LOCALAPPDATA\WeMod\app-*" -Directory |
         Where-Object { Test-Path "$_\resources\app.asar" } |
         Sort-Object { [version]($_.Name -replace '^app-','') } -Descending |
         Select-Object -First 1
python bin\wemod_enhancer.py restore --install-dir $wemod.FullName
```

## Comparison with Wand-Enhancer

Choose **WeMod Enhancer** if you only need Pro unlocked and want a lightweight, scriptable tool that runs on every supported platform. Choose [Wand-Enhancer](https://github.com/k1tbyte/Wand-Enhancer) if you use Windows and need extras such as the Remote Web Panel or custom script injection.

| Aspect                  | WeMod Enhancer                                          | Wand-Enhancer                                      |
| ----------------------- | ------------------------------------------------------- | -------------------------------------------------- |
| Language                | Python (stdlib only)                                    | C# / .NET / WPF                                    |
| Runtime dependencies    | None (Python stdlib only)                               | .NET Framework 4.8 runtime                         |
| Build dependencies      | CMake, compiler                                         | CMake + Node.js + pnpm + VS 2022 + MSBuild + NuGet |
| Build from source       | cmake --workflow --preset windows_llvm_mingw_amd64_full | Fork → GitHub Actions → download artifact          |
| Platform                | Steam Deck, Linux, Windows                              | Windows only                                       |
| Interface               | CLI (scriptable, automatable)                           | WPF GUI (click-through wizard)                     |
| Binary size             | Single Python script                                    | Full .NET WPF application                          |
| ASAR integrity bypass   | On-disk fuse flip (`1` → `r`), no DLL                   | In-process fuse flip via DLL                       |
| Pro activation          | Intercepts `/v3/account` responses                      | Same                                               |
| DevTools                | F12 hotkey hook                                         | Not available                                      |
| Disable updates         | `ACTION_CHECK_FOR_UPDATE` → no-op                       | Not available                                      |
| Disable mobile pairing  | `requestRemoteAuthCode()` → reject                      | Not available                                      |
| Remote web panel        | Not included                                            | Built-in LAN HTTP/WebSocket server                 |
| Custom script injection | Not included                                            | Bundled `.js` injection at patch time              |
| Fail-safe               | Fails closed on mismatched patches                      | Not documented                                     |
| Backup and restore      | Automatic, one-command restore                          | Not documented                                     |
| License                 | MIT                                                     | Apache-2.0                                         |

## Build from source

This section is intended for builders, maintainers, and contributors. Users should download a prebuilt package from the [latest release](https://github.com/e-gleba/wemod_enhancer/releases/latest).

**Requirements:** CMake 3.31+ and Ninja. On Linux, the LLVM-MinGW toolchain is downloaded automatically.

To build the Linux-to-Windows package:

```sh
cmake --workflow --preset windows_llvm_mingw_amd64_full
```

To build the Windows (MSVC) package:

```sh
cmake --workflow --preset windows_msvc_amd64_full
```

GUI builds on Linux hosts require the SDL3 Wayland/X11 development packages (build-time only). See [cmake_multi_platform.yml](.github/workflows/cmake_multi_platform.yml) for the exact apt package list. On Fedora, install the static C++ runtime with `sudo dnf install libstdc++-static`.

---

<div align="center">
<sub>MIT © 2026 Evgeniy Gleba · Not affiliated with WeMod.</sub>
</div>
