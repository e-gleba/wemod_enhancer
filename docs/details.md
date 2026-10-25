# Details

For users: README covers install and use. This page keeps the technical context that used to sit in the README so it stays easy to find without making first-time setup harder.

## How it works

| Piece | What happens |
| :---- | :----------- |
| Pro activation | Intercepts `/v3/account` and injects `subscription: { period: "yearly", state: "active" }` |
| ASAR integrity bypass | 4 KB `version.dll` proxy flips Electron's `EnableEmbeddedAsarIntegrityValidation` fuse in-process via `VirtualProtect` |
| F12 DevTools | Hotkey hook on `browser-window-created` |
| No auto-updates | `ACTION_CHECK_FOR_UPDATE` becomes a no-op so the patch survives until you update on purpose |
| No mobile pairing | `requestRemoteAuthCode()` rejects instead of showing the pairing prompt |
| Fail-safe | Automatic backup, all-or-nothing patching, one-command restore |

## Linux launch

WeMod has no Linux build, so [wemod-launcher](https://github.com/DeckCheatz/wemod-launcher) runs the official Windows client through Proton. WeMod Enhancer patches that install in place.

The important gotcha is install order: `wemod_bin` only appears after you launch a game through wemod-launcher at least once and log in to your WeMod account. The launcher downloads the client into `~/wemod-launcher/wemod_data/wemod_bin` on first run, so patch only after that folder exists. See the [wemod-launcher quick guide](https://github.com/DeckCheatz/wemod-launcher#quick-guide) for Proton, launch options, and first login.

Steam launch options for the game:

```sh
WINEDLLOVERRIDES="version=n,b" WEMOD_LOG="$HOME/wemod-launcher/wemod.log" "$HOME/wemod-launcher/wemod" %command%
```

`version=n,b` forces native-first DLL loading. Without it Wine uses its builtin `version.dll` and Electron rejects the patched ASAR.

Full diagnostics, log locations, and cleanup: [linux.md](linux.md).

## Windows DLL search order

> [!WARNING]
> Unsafe, last-resort workaround. Only if patching alone does not load `version.dll`.

With safe DLL search mode enabled, which is the Windows default, the current folder is searched last, after the app folder, System32, and the Windows folder. A copy found earlier can win over your current-folder `version.dll`.

If that is your case, as a last resort run elevated and restart Windows:

```powershell
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager" /v SafeDllSearchMode /t REG_DWORD /d 0 /f
```

To restore the safe default, run the same command elevated with `/d 1` and restart again. The patched DLL will stop loading until you disable it again.

While disabled, the current folder moves to just after the app folder for every app, which reopens [DLL preloading / binary planting attacks](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-security). Never run patched WeMod elevated. See [DLL search order](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order).

## Comparison with Wand-Enhancer

Ground-up rewrite of the original [Wand-Enhancer](https://github.com/k1tbyte/Wand-Enhancer). Same core job, no runtime dependencies, cross-platform.

| | WeMod Enhancer | Wand-Enhancer |
| :-- | :------------- | :------------ |
| Language | C + Python, stdlib only | C# / .NET / WPF |
| Runtime deps | None, Python stdlib + 4 KB C DLL | .NET Framework 4.8 |
| Build deps | CMake + compiler | CMake + Node.js + pnpm + VS 2022 + MSBuild + NuGet |
| Build from source | `cmake --workflow --preset windows_llvm_mingw_amd64_full` | Fork, GitHub Actions, download artifact |
| Platform | Steam Deck, Linux, Windows | Windows only |
| Interface | CLI, scriptable | WPF GUI wizard |
| Binary size | ~4 KB proxy DLL | Full .NET WPF app |
| ASAR integrity bypass | In-process fuse flip via `VirtualProtect` | Same approach, shared heritage |
| Pro activation | Intercepts `/v3/account` | Same |
| DevTools | F12 hotkey hook | Not available |
| Disable updates | `ACTION_CHECK_FOR_UPDATE` no-op | Not available |
| Disable mobile pairing | `requestRemoteAuthCode()` rejects | Not available |
| Remote web panel | Not included | Built-in LAN HTTP/WebSocket server |
| Custom script injection | Not included | Bundled `.js` injection at patch time |
| Fail-safe | Fails closed on mismatched patches | — |
| Backup and restore | Automatic, one command | — |
| License | MIT | Apache-2.0 |

If you need the remote web panel or custom script injection on Windows, Wand-Enhancer is still a solid choice. WeMod Enhancer keeps the core patching pipeline as small as possible.

## Build from source

Users should use a prebuilt package from the [latest release](https://github.com/e-gleba/wemod_enhancer/releases/latest). This section is for builders and contributors.

Needs CMake 3.31+ and Ninja. On Linux the LLVM-MinGW toolchain is auto-downloaded.

```sh
cmake --workflow --preset windows_llvm_mingw_amd64_full   # Linux → Windows package
cmake --workflow --preset windows_msvc_amd64_full         # Windows (MSVC) package
```

GUI builds on Linux hosts need SDL3 Wayland/X11 dev packages, build-time only. See [cmake_multi_platform.yml](../.github/workflows/cmake_multi_platform.yml) for the exact apt list. Fedora needs `libstdc++-static` for the static C++ runtime: `sudo dnf install libstdc++-static`.
