<div align="center">

<img src="docs/logo.svg" alt="WeMod Enhancer" width="128">

# WeMod Enhancer

**WeMod Pro. On Steam Deck, Linux, and Windows.**

Download, patch once, play. Pro stays on, forced updates and the mobile-pairing nag stay off, and you can undo everything in one command.

[Download the latest release](https://github.com/e-gleba/wemod_enhancer/releases/latest) · [Steam Deck & Linux](#steam-deck--linux) · [Windows](#windows) · [Restore](#restore)

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](./license.md)
[![release](https://img.shields.io/github/v/release/e-gleba/wemod_enhancer)](https://github.com/e-gleba/wemod_enhancer/releases)
[![ci](https://img.shields.io/github/actions/workflow/status/e-gleba/wemod_enhancer/cmake_multi_platform.yml?branch=main&label=ci)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/cmake_multi_platform.yml)
[![Platform](https://img.shields.io/badge/Platform-Steam_Deck_·_Linux_·_Windows-121212?logo=steam&logoColor=white)](#steam-deck--linux)

</div>

## What you get

- **Pro, unlocked** — the original WeMod app with Pro active, no subscription required.
- **WeMod where it does not run** — Steam Deck and Linux, through Proton.
- **No surprises** — auto-updates off, mobile-pairing nag off, your patch stays until you change it.
- **DevTools when you need them** — press F12 in the client.
- **Safe to try** — automatic backup before patching, one command to restore the original.

This is the original client, patched in place. Nothing is downloaded at runtime and there are no runtime dependencies.

## Get started

### GUI — recommended

![Screenshot presenting GUI](docs/Screenshot_20260930_224227.png)

Download the package for your system from the [latest release](https://github.com/e-gleba/wemod_enhancer/releases/latest), unpack it, and run the app. No terminal needed.

| System | Download | Run |
| :----- | :------- | :-- |
| Windows x64 | `wemod_enhancer-windows-msvc-amd64.zip` | `bin/wemod_enhancer_gui.exe` |
| Linux x64 | `wemod_enhancer-linux-amd64.tar.xz` | `bin/wemod_enhancer_gui.elf` |
| Linux ARM64 | `wemod_enhancer-linux-arm64.tar.xz` | `bin/wemod_enhancer_gui.elf` |

The app finds everything it needs next to itself, detects your WeMod install, and shows the patch output live. If something fails, **Report bug** opens a pre-filled issue with the log attached.

You only need [Python 3.11+](https://python.org) on your `PATH`.

### Steam Deck & Linux

WeMod has no Linux build, so this runs the official Windows client through Proton with [wemod-launcher](https://github.com/DeckCheatz/wemod-launcher).

1. Install wemod-launcher and launch a game through it once. Log in to WeMod when it opens.

```sh
git clone https://github.com/DeckCheatz/wemod-launcher "$HOME/wemod-launcher"
chmod +x "$HOME/wemod-launcher/wemod"
```

Follow the [wemod-launcher tutorial](https://github.com/DeckCheatz/wemod-launcher#quick-guide) for Proton and first-login setup.

2. Download WeMod Enhancer and unpack it.

```sh
curl -LO https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-windows-llvm-mingw-amd64.tar.xz
mkdir -p wemod_enhancer && tar -xf wemod_enhancer-windows-llvm-mingw-amd64.tar.xz -C wemod_enhancer
cd wemod_enhancer
```

3. Patch the WeMod install, or open the GUI instead.

```sh
python3 bin/wemod_enhancer.py patch --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
# or
./bin/wemod_enhancer_gui.elf
```

> `wemod_bin` only exists after the first launch in step 1 — the launcher downloads WeMod there. Patch only once that folder is present. No `python3`? It is preinstalled on SteamOS and most distros.

4. Set the game's Steam launch options.

```sh
WINEDLLOVERRIDES="version=n,b" WEMOD_LOG="$HOME/wemod-launcher/wemod.log" "$HOME/wemod-launcher/wemod" %command%
```

Done. WeMod starts with the game, Pro active. Logs, diagnostics, and cleanup: [docs/linux.md](docs/linux.md).

### Windows

Install WeMod and log in. Close WeMod, open **PowerShell**, and paste:

```powershell
# 1. Download and unpack WeMod Enhancer
cd $env:USERPROFILE\Downloads
Invoke-WebRequest -Uri "https://github.com/e-gleba/wemod_enhancer/releases/latest/download/wemod_enhancer-windows-msvc-amd64.zip" -OutFile "wemod_enhancer.zip"
Expand-Archive wemod_enhancer.zip -DestinationPath wemod_enhancer -Force
cd wemod_enhancer

# 2. Find the newest WeMod install and patch it
$wemod = Get-ChildItem "$env:LOCALAPPDATA\WeMod\app-*" -Directory |
         Where-Object { Test-Path "$_\resources\app.asar" } |
         Sort-Object { [version]($_.Name -replace '^app-','') } -Descending |
         Select-Object -First 1
python bin\wemod_enhancer.py patch --install-dir $wemod.FullName
# or launch the GUI
.\bin\wemod_enhancer_gui.exe
```

> `python` not recognized? Run `winget install Python.Python.3.13`, reopen PowerShell, and try again. Patched, but WeMod does not start? See the Windows note in [docs/details.md](docs/details.md#windows-dll-search-order).

## Restore

Changed your mind, or something looks wrong? Originals are backed up automatically. One command puts everything back:

```sh
# Linux
python3 bin/wemod_enhancer.py restore --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
```

```powershell
# Windows, in the same PowerShell session as above
python bin\wemod_enhancer.py restore --install-dir $wemod.FullName
```

## For developers

How the patch works, the Wand-Enhancer comparison, the Windows DLL search-order workaround, and build-from-source instructions live in [docs/details.md](docs/details.md). Most users never need them.

<div align="center">
<sub>MIT © 2026 Evgeniy Gleba · Not affiliated with WeMod.</sub>
</div>
