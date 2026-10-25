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
python3 scripts/wemod_enhancer.py patch \
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
"$HOME/wemod-launcher/wemod" %command%
```

No `WINEDLLOVERRIDES` is needed — the patcher flips the ASAR integrity fuse in `WeMod.exe` itself, so there is no extra DLL for Wine to ignore.

## Diagnostic Steam launch

Use this only while investigating a launch failure:

```sh
WINEDEBUG=+seh,+loaddll WEMOD_LOG="$HOME/wemod-launcher/wemod.log" PROTON_LOG=1 "$HOME/wemod-launcher/wemod" %command%
```

Variables:

- `WINEDEBUG=+seh,+loaddll`: log Windows exceptions and DLL loading; output is large
- `WEMOD_LOG=...`: put launcher log at a known path
- `PROTON_LOG=1`: write Steam/Proton log, normally `$HOME/steam-<APPID>.log`

## Confirm the fuse flip

After patching, the executable backup must exist next to `WeMod.exe`:

```sh
ls -l "$HOME/wemod-launcher/wemod_data/wemod_bin/WeMod.exe.wemod-enhancer-backup"
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
  'WeMod\\.exe|app\\.asar|unhandled|fatal|exception|page fault|SyntaxError|ReferenceError|TypeError|unexpected token|err:' \
  "$HOME/steam-<APPID>.log" | tail -n 300
```

Wine RPC errors such as `device_notify_proc failed to get event, error 1726` often occur during shutdown and may not be the first failure. Inspect earlier lines.

## Restore and clean up

Restore the original ASAR and executable:

```sh
python3 scripts/wemod_enhancer.py restore \
  --install-dir "$HOME/wemod-launcher/wemod_data/wemod_bin"
```

Remove diagnostic logs:

```sh
rm -f "$HOME"/steam-*.log
rm -f "$HOME/wemod-launcher/wemod.log"
```

Remove local build products only when no build is running:

```sh
rm -rf build
```

Do not delete `wemod_data/wemod_login`; it contains shared WeMod login data. Do not delete a Steam compatibility prefix unless you have backed up saves and intentionally want wemod-launcher to rebuild it.

## Return to normal launch

After diagnosis, remove `WINEDEBUG` and `PROTON_LOG` to avoid large logs:

```sh
"$HOME/wemod-launcher/wemod" %command%
```

## Development notes

The Linux path was developed against DeckCheatz/wemod-launcher and Proton. The launcher parses Steam's `%command%`, prepares or reuses the game prefix, synchronizes login data, then runs `wemod.bat` and the game under the selected Proton build.

The Python tool backs up `app.asar` and `WeMod.exe`, applies supported JavaScript changes, rebuilds ASAR metadata, flips Electron's ASAR-integrity fuse (`1` to `r`) in the executable, and can restore the original files. No extra DLL is installed, so no `WINEDLLOVERRIDES` is needed on Proton/Wine.
