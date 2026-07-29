# WeMod Enhancer

Build and patch tooling for Wand/WeMod's Windows x86-64 Electron client. The Python patcher runs on Windows or Linux; Linux cross-compiles `version.dll` with LLVM-MinGW and launches WeMod through Wine/Proton.

## Requirements

- Python 3.11+
- CMake 3.31+
- Ninja
- Windows: MSVC or LLVM-MinGW
- Linux: LLVM-MinGW is downloaded by the included CMake toolchain

## Patch

Stop WeMod, then pass the directory containing `WeMod.exe` and `resources/app.asar`:

```sh
python3 tools/wemod_enhancer.py patch --install-dir "/path/to/wemod_bin"
```

Restore original files:

```sh
python3 tools/wemod_enhancer.py restore --install-dir "/path/to/wemod_bin"
```

Build only the proxy DLL:

```sh
python3 tools/wemod_enhancer.py build-dll
```

Backups are created before modification. Client updates can change minified JavaScript; unsupported patch targets fail closed.

## Linux and Steam

Install the maintained Linux launcher:

```sh
git clone https://github.com/DeckCheatz/wemod-launcher "$HOME/wemod-launcher"
chmod +x "$HOME/wemod-launcher/wemod"
```

Follow its setup, run the target game once, and select a compatible Proton or GE-Proton version. WeMod is normally stored at:

```text
$HOME/wemod-launcher/wemod_data/wemod_bin
```

Patch it:

```sh
python3 tools/wemod_enhancer.py patch \
  --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
```

Set the Steam game's launch options to:

```sh
WINEDLLOVERRIDES="version=n,b" "$HOME/wemod-launcher/wemod" %command%
```

The override is required under Wine/Proton: `n,b` loads the native proxy beside `WeMod.exe` first and falls back to Wine's builtin `version.dll`. Without it, Wine can load only its builtin DLL and Electron will reject the modified ASAR.

Detailed diagnostics, log locations, cleanup, and implementation history: [Linux launch and debugging](docs/linux.md)

## Development

The project originated as a focused port of the native ASAR-fuse proxy and applicable JavaScript patches from Wand-Enhancer. Development proceeded by:

1. replacing the generic C++ template application with a Windows x86-64 proxy DLL;
2. adding Linux-to-Windows LLVM-MinGW and native Windows MSVC builds;
3. porting backup, patch, restore, and ASAR handling to one standard-library Python CLI;
4. validating behavior against the original AsarSharp pickle and SHA-256 integrity implementation;
5. diagnosing Proton startup with `PROTON_LOG`, `WINEDEBUG`, and DLL-load traces;
6. documenting Wine's required native-first `version.dll` override.

The CMake and CI scope is intentionally limited to Windows x86-64. Linux is a host and runtime environment, not a native DLL target.
