# WeMod Enhancer

Cross-platform build and patch tooling for Wand/WeMod's Windows Electron client.
The patcher runs on Windows, Linux, or macOS; generated `version.dll` is Windows x86-64 and can be used under Wine/Proton.

## Requirements

- Python 3.11+
- CMake 3.31+
- Ninja
- Windows: LLVM-MinGW compiler on `PATH`
- Linux: existing toolchain file downloads LLVM-MinGW automatically

## Build DLL

```sh
python3 tools/wemod_enhancer.py build-dll
```

## Patch

Stop Wand/WeMod first, then pass its directory—the directory containing `resources/app.asar`:

```sh
python3 tools/wemod_enhancer.py patch --install-dir "/path/to/Wand"
```

Use a prebuilt DLL:

```sh
python3 tools/wemod_enhancer.py patch --install-dir "/path/to/Wand" --version-dll ./version.dll
```

For Wine/Proton, pass the Windows application directory inside the prefix, for example:

```sh
python3 tools/wemod_enhancer.py patch --install-dir "$STEAMLIBRARY/steamapps/compatdata/ID/pfx/drive_c/users/steamuser/AppData/Local/Wand"
```

Restore:

```sh
python3 tools/wemod_enhancer.py restore --install-dir "/path/to/Wand"
```

Backups are created before modification. Client updates may change minified JavaScript; the patcher fails closed when exact targets cannot be found.
