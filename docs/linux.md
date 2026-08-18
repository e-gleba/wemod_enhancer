# Linux launch and debugging

## Install wemod-launcher

Clone the maintained Linux launcher:

```sh
git clone https://github.com/DeckCheatz/wemod-launcher "$HOME/wemod-launcher"
chmod +x "$HOME/wemod-launcher/wemod"
```

Follow its setup instructions, run the target game once without WeMod, and configure a compatible Proton or GE-Proton version. The Steam launch command must invoke `wemod-launcher`; do not launch `WeMod.exe` directly.

## Patch WeMod

From this repository:

```sh
python3 scripts/wemod_enhancer patch \
  --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
```

The install directory must contain:

```text
WeMod.exe
resources/app.asar
```

## Normal Steam launch

Set the game's Steam launch options to:

```sh
WINEDLLOVERRIDES="version=n,b" "$HOME/wemod-launcher/wemod" %command%
```

`WINEDLLOVERRIDES="version=n,b"` means native first, builtin fallback. This is necessary because Wine can otherwise satisfy `version.dll` with its builtin implementation even when the proxy exists beside `WeMod.exe`.

## Diagnostic Steam launch

Use this only while investigating a launch failure:

```sh
WINEDLLOVERRIDES="version=n,b" WINEDEBUG=+seh,+loaddll WEMOD_LOG="$HOME/wemod-launcher/wemod.log" PROTON_LOG=1 "$HOME/wemod-launcher/wemod" %command%
```

Variables:

- `WINEDLLOVERRIDES="version=n,b"`: prefer enhancer proxy, retain builtin fallback
- `WINEDEBUG=+seh,+loaddll`: log Windows exceptions and DLL loading; output is large
- `WEMOD_LOG=...`: put launcher log at a known path
- `PROTON_LOG=1`: write Steam/Proton log, normally `$HOME/steam-<APPID>.log`

## Confirm the proxy loaded

After launching, replace `<APPID>` with the Steam application ID:

```sh
grep -i 'version\.dll' "$HOME/steam-<APPID>.log" | tail -n 30
```

Expected for `WeMod.exe`:

```text
...wemod_bin\\version.dll... native
```

If the line ends in `builtin`, the proxy did not load. Check the Steam launch options and ensure this file exists:

```sh
ls -l "$HOME/wemod-launcher/wemod_data/wemod_bin/version.dll"
```

## Read logs

Launcher log:

```sh
tail -n 250 "$HOME/wemod-launcher/wemod.log"
```

Proton log:

```sh
less "$HOME/steam-<APPID>.log"
```

Useful filter:

```sh
grep -nEi \
  'WeMod\.exe|version\.dll|app\.asar|unhandled|fatal|exception|page fault|SyntaxError|ReferenceError|TypeError|unexpected token|err:' \
  "$HOME/steam-<APPID>.log" | tail -n 300
```

Wine RPC errors such as `device_notify_proc failed to get event, error 1726` often occur during shutdown and may not be the first failure. Inspect earlier lines.

## Restore and clean up

Restore the original ASAR and prior DLL state:

```sh
python3 scripts/wemod_enhancer restore \
  --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
```

Remove diagnostic logs:

```sh
rm -f "$HOME"/steam-*.log
rm -f "$HOME/wemod-launcher/wemod.log"
```

Remove local build products and downloaded LLVM-MinGW only when no build is running:

```sh
rm -rf build llvm_mingw
```

Do not delete `wemod_data/wemod_login`; it contains shared WeMod login data. Do not delete a Steam compatibility prefix unless you have backed up saves and intentionally want wemod-launcher to rebuild it.

## Return to normal launch

After diagnosis, remove `WINEDEBUG` and `PROTON_LOG` to avoid large logs. Keep the native-first override:

```sh
WINEDLLOVERRIDES="version=n,b" "$HOME/wemod-launcher/wemod" %command%
```

## Development notes

The Linux path was developed against DeckCheatz/wemod-launcher and Proton. The launcher parses Steam's `%command%`, prepares or reuses the game prefix, synchronizes login data, then runs `wemod.bat` and the game under the selected Proton build.

The enhancer's proxy is a Windows x86-64 DLL. Linux builds it with LLVM-MinGW; Proton loads it as a native Windows DLL. The proxy forwards Windows version APIs and changes Electron's ASAR-integrity fuse in the WeMod process. The Python tool backs up `app.asar`, applies supported JavaScript changes, rebuilds ASAR metadata, installs the proxy, and can restore the original files.

Debugging established an important Wine-specific requirement: placing `version.dll` beside `WeMod.exe` is insufficient when Wine chooses its builtin library. The launch environment must request native-first loading with `WINEDLLOVERRIDES="version=n,b"`.
