<div align="center">

<img src="docs/logo.svg" alt="WeMod Enhancer logo" width="128">

# WeMod Enhancer

**Unlock WeMod Pro on Steam Deck, Linux, and Windows.**

One command patches the WeMod client to enable Pro, disable auto-updates, enable the F12 DevTools, and remove the mobile-pairing prompt. The only requirement is [Python](https://www.python.org/downloads/).

[![CI](https://img.shields.io/github/actions/workflow/status/e-gleba/wemod_enhancer/cmake_multi_platform.yml?branch=main&label=CI)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/cmake_multi_platform.yml)
[![Release](https://img.shields.io/github/v/release/e-gleba/wemod_enhancer)](https://github.com/e-gleba/wemod_enhancer/releases)
[![Platform](https://img.shields.io/badge/Platform-Steam_Deck_·_Linux_·_Windows-121212?logo=steam&logoColor=white)](#windows)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

[![Run release](https://img.shields.io/badge/%E2%96%B6_Run-Release-2ea44f)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/publish_release.yml)
[![Run CI](https://img.shields.io/badge/%E2%96%B6_Run-CI-2ea44f)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/cmake_multi_platform.yml)
[![Run Renovate](https://img.shields.io/badge/%E2%96%B6_Run-Renovate-2ea44f)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/renovate.yml)

![WeMod Enhancer GUI](docs/Screenshot_20260930_224227.png)

</div>

## Contents

- [Windows](#windows)
- [Linux / Steam Deck](#linux--steam-deck)
- [Comparison with Wand-Enhancer](#comparison-with-wand-enhancer)
- [Build from source](#build-from-source)

## Windows

**Requirements:** [Python 3.11+](https://www.python.org/downloads/) available on `PATH`. If `python` is not recognized, install [Python 3.13](https://www.python.org/downloads/) (`winget install Python.Python.3.13`) and reopen your terminal. To remove Python after patching, run `winget uninstall Python.Python.3.13`.

1. Install WeMod and log in to your WeMod account at least once.
2. Close WeMod.
3. Download [`wemod_enhancer-windows-msvc-amd64.zip`][win-x64] from the [latest release](https://github.com/e-gleba/wemod_enhancer/releases/latest) and extract it.
4. Patch WeMod with the GUI (recommended) or the command line. Choose either one—you do not need both.

### GUI (recommended)

The GUI ships as a self-contained folder and requires no terminal. All components are included in the package, and nothing is downloaded at runtime.

1. Run `bin\wemod_enhancer_gui.exe`.
2. The WeMod installation folder is detected automatically. If it is not, select it with **Browse...**.
3. Click **Patch**.

To revert the changes later, click **Restore** in the same window. Do not click it while patching; it is only for undoing the patch.

The GUI locates `wemod_enhancer.py` next to the executable and streams patch output live. **Report bug** opens a pre-filled GitHub issue with the log attached.

### Command line (advanced)

Run the patcher from the extracted folder, passing the newest WeMod `app-*` folder that contains `resources\app.asar`:

```powershell
python bin\wemod_enhancer.py patch --install-dir "C:\Users\<user>\AppData\Local\WeMod\app-<version>"
```

Original files are backed up automatically. To revert the changes, run:

```powershell
python bin\wemod_enhancer.py restore --install-dir "C:\Users\<user>\AppData\Local\WeMod\app-<version>"
```

## Linux / Steam Deck

WeMod does not provide a native Linux build. [wemod-launcher](https://github.com/DeckCheatz/wemod-launcher) runs the official Windows client through Proton, and WeMod Enhancer then unlocks Pro on top of it.

**Requirements:** [Python 3.11+](https://www.python.org/downloads/) available on `PATH`. If `python3` is not installed, install Python 3.11+ with your package manager. It is preinstalled on SteamOS and most major distributions.

1. **Install the launcher**

   ```sh
   git clone https://github.com/DeckCheatz/wemod-launcher "$HOME/wemod-launcher"
   chmod +x "$HOME/wemod-launcher/wemod"
   ```

   Follow the [wemod-launcher quick guide](https://github.com/DeckCheatz/wemod-launcher#quick-guide) for setup.

   > **Note:** ProtonQT is not required. You can use vanilla Proton if you encounter problems with it.

2. **Set the Steam launch options** for the game (better do it on some offline game to avoid anti-cheat problems):

   ```sh
   "$HOME/wemod-launcher/wemod" %command%
   ```
   > **Note:** To see wemod log you can add as a first launch option `WEMOD_LOG="$HOME/wemod-launcher/wemod.log"`

3. **Launch the game and log in to WeMod.**

   > **Important:** The `wemod_bin` directory is created only after you launch a game through wemod-launcher at least once and log in to your WeMod account. The launcher downloads the WeMod client to `~/wemod-launcher/wemod_data/wemod_bin` on first run. Patch only after this directory exists.

4. **Download WeMod Enhancer** from the [latest release](https://github.com/e-gleba/wemod_enhancer/releases/latest) and extract it:

   | Architecture | Artifact                                           |
   | ------------ | -------------------------------------------------- |
   | Linux x64    | [`wemod_enhancer-linux-amd64.tar.xz`][linux-x64]   |
   | Linux ARM64  | [`wemod_enhancer-linux-arm64.tar.xz`][linux-arm64] |

5. **Patch the WeMod installation** with the GUI (recommended) or the command line. Choose either one—you do not need both.

### GUI (recommended)

The GUI ships as a self-contained folder and requires no terminal. All components are included in the package, and nothing is downloaded at runtime.

1. Run the GUI from the extracted folder:

   ```sh
   ./bin/wemod_enhancer_gui.elf
   ```

2. Select `~/wemod-launcher/wemod_data/wemod_bin` with **Browse...** if it is not detected automatically.
3. Click **Patch**.

To revert the changes later, click **Restore** in the same window. Do not click it while patching; it is only for undoing the patch.

The GUI locates `wemod_enhancer.py` next to the executable and streams patch output live. **Report bug** opens a pre-filled GitHub issue with the log attached.

### Command line (advanced)

```sh
python3 bin/wemod_enhancer.py patch --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
```

Original files are backed up automatically. To revert the changes, run:

```sh
python3 bin/wemod_enhancer.py restore --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
```

[win-x64]: https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-windows-msvc-amd64.zip
[linux-x64]: https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-linux-amd64.tar.xz
[linux-arm64]: https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-linux-arm64.tar.xz

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
