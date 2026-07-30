<div align="center">

<img src="docs/logo.svg" alt="WeMod Enhancer" width="180">

# WeMod Enhancer

**Cross-platform patcher and ASAR-fuse proxy for Wand/WeMod's Electron client**

A Python CLI that patches WeMod's Windows x86-64 Electron app in-place — disabling ASAR integrity, activating Pro features, killing auto-updates, blocking native mobile pairing, and enabling F12 DevTools. On Linux it cross-compiles the `version.dll` proxy with LLVM-MinGW and launches through Wine/Proton.

[![CI](https://github.com/e-gleba/wemod_enhancer/actions/workflows/cmake_multi_platform.yml/badge.svg)](https://github.com/e-gleba/wemod_enhancer/actions/workflows/cmake_multi_platform.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](license.md)
[![CMake](https://img.shields.io/badge/CMake-3.31+-064F8C?logo=cmake&logoColor=white)](https://cmake.org/)
[![C++](https://img.shields.io/badge/C-23-00599C?logo=c&logoColor=white)](https://en.cppreference.com/)
[![Python](https://img.shields.io/badge/Python-3.11+-3776AB?logo=python&logoColor=white)](https://www.python.org/)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20Steam%20Deck-121212?logo=steam&logoColor=white)](#linux-and-steam)

</div>

---

## What it does

| Patch | Effect |
|:------|:-------|
| **ASAR Integrity Bypass** | Flips Electron's `RunAsNode` / integrity fuse to `removed` via an in-process `version.dll` proxy — no more modified-ASAR rejection |
| **Pro Account Activation** | Intercepts `/v3/account` responses and injects `subscription: { period: "yearly", state: "active" }` |
| **Pro Language Activation** | Same Pro injection on the language endpoint |
| **Disable Updates** | Replaces `ACTION_CHECK_FOR_UPDATE` handler with a no-op — WeMod stops checking for client updates |
| **Disable Native Pairing** | `requestRemoteAuthCode()` returns `Promise.reject` — blocks mobile app pairing requests |
| **DevTools F12** | Hooks `browser-window-created` to toggle DevTools on F12 keydown in any WeMod window |

> **Note** — Pro brand experience (`setAccountWandBrandExperience`) is an optional patch applied only when the endpoint exists in the current client build.

## Quick Start

```sh
# 1. Stop WeMod, then patch the install directory
python3 tools/wemod_enhancer.py patch --install-dir "/path/to/wemod_bin"

# 2. Restore originals at any time
python3 tools/wemod_enhancer.py restore --install-dir "/path/to/wemod_bin"

# 3. Build only the proxy DLL (no patching)
python3 tools/wemod_enhancer.py build-dll
```

Backups are created automatically before modification. If a client update changes minified JavaScript and a required patch no longer matches, the tool **fails closed** — no partial patches are applied.

## Requirements

| Tool | Version | Notes |
|:-----|:--------|:------|
| **Python** | 3.11+ | Runs the patcher CLI |
| **CMake** | 3.31+ | Builds the proxy DLL |
| **Ninja** | any | Build system generator (required for Clang preset) |
| **Windows** | — | MSVC (VS 2022) or Clang targeting MSVC ABI (`x86_64-pc-windows-msvc`) |
| **Linux** | — | LLVM-MinGW toolchain, auto-downloaded by CMake |

## Linux and Steam

For Steam Deck / Linux gaming, use the maintained [wemod-launcher](https://github.com/DeckCheatz/wemod-launcher):

```sh
# Install the launcher
git clone https://github.com/DeckCheatz/wemod-launcher "$HOME/wemod-launcher"
chmod +x "$HOME/wemod-launcher/wemod"

# Patch WeMod
python3 tools/wemod_enhancer.py patch \
  --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
```

Set your Steam game launch options to:

```sh
WINEDLLOVERRIDES="version=n,b" "$HOME/wemod-launcher/wemod" %command%
```

> **Why `version=n,b`?** Wine can satisfy `version.dll` with its builtin implementation even when the proxy exists beside `WeMod.exe`. The `n,b` override forces native-first loading with builtin fallback — without it, Wine loads only its builtin DLL and Electron rejects the modified ASAR.

For detailed diagnostics, log locations, and cleanup, see [Linux launch and debugging](docs/linux.md).

## How it works

```mermaid
flowchart TD
    subgraph CLI["tools/wemod_enhancer.py"]
        A["Backup app.asar → app.asar.backup"] --> B["Extract ASAR<br/>(pickle + SHA-256)"]
        B --> C["Apply JavaScript patches<br/>(regex on minified bundles)"]
        C --> D["Rebuild ASAR<br/>with fresh integrity hashes"]
        D --> E["Install version.dll proxy"]
        E --> F["Verify PE x86-64 header"]
    end

    subgraph DLL["src/version.dll — loaded in-process"]
        G["Forward all version.dll exports"]
        G --> H["Scan process memory<br/>for 32-byte fuse sentinel"]
        H --> I["Flip ASAR-integrity fuse<br/>to 'removed' via VirtualProtect"]
    end

    F --> G

    style CLI fill:#0d1117,stroke:#30363d,color:#e6edf3
    style DLL fill:#0d1117,stroke:#f78166,color:#e6edf3
    style A fill:#161b22,stroke:#30363d,color:#e6edf3
    style B fill:#161b22,stroke:#30363d,color:#e6edf3
    style C fill:#161b22,stroke:#30363d,color:#e6edf3
    style D fill:#161b22,stroke:#30363d,color:#e6edf3
    style E fill:#161b22,stroke:#f78166,color:#e6edf3
    style F fill:#161b22,stroke:#30363d,color:#e6edf3
    style G fill:#161b22,stroke:#f78166,color:#e6edf3
    style H fill:#161b22,stroke:#30363d,color:#e6edf3
    style I fill:#161b22,stroke:#f78166,color:#e6edf3
```

The proxy DLL (`src/fuses.c`) walks the loaded module's memory for a 32-byte sentinel sequence, locates Electron's fuse wire structure, and flips the `RunAsNode`/integrity fuse from `enabled` to `removed` using `VirtualProtect` — all in-process, no external debugger required.

## Development

The project originated as a focused port of the native ASAR-fuse proxy and JavaScript patches from [Wand-Enhancer](https://github.com/e-gleba/wemod_enhancer). Development proceeded by:

1. Replacing the generic C++ template with a Windows x86-64 proxy DLL
2. Adding Linux-to-Windows LLVM-MinGW and native Windows MSVC builds
3. Porting backup, patch, restore, and ASAR handling to one standard-library Python CLI
4. Validating behavior against the original AsarSharp pickle and SHA-256 integrity implementation
5. Diagnosing Proton startup with `PROTON_LOG`, `WINEDEBUG`, and DLL-load traces
6. Documenting Wine's required native-first `version.dll` override

The CMake and CI scope is intentionally limited to Windows x86-64. Linux is a host and runtime environment, not a native DLL target.

### Code Quality

```sh
cmake --build build/gcc --target format   # clang-format + cmake-format
cmake --build build/gcc --target tidy     # clang-tidy
cmake --build build/gcc --target cpplint  # cppcheck/cpplint
```

Pre-commit hooks enforce formatting and linting. Run `pre-commit install` once after cloning.

## License

[MIT](license.md) — © 2026 Evgeniy Gleba
