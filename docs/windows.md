# Windows notes

## Where WeMod lives

The client ships as **Wand** (WeMod rebranded to Wand in 12.x). The install
folder is:

```
%LOCALAPPDATA%\Wand\app-<version>\
```

`%LOCALAPPDATA%\WeMod\app-*` is the pre-rebrand path and is still valid for
older installs.

## The app does not start after patching

Symptom — the process dies immediately, before any window appears:

```
[FATAL:electron\shell\common\asar\asar_util.cc:143] Integrity check failed for
asar archive (<hash embedded in Wand.exe> vs <hash of the patched header>)
```

Cause — Electron validates the ASAR **header** against a SHA256 that the
publisher embedded in the executable. Patching `app.asar` changes the header,
so the embedded hash no longer matches and Electron aborts the process.

The bypass is a fuse (`EnableEmbeddedAsarIntegrityValidation`, index 4 of the
Electron fuse wire). Two ways to remove it:

| Way | How | Notes |
| --- | --- | --- |
| Runtime | `version.dll` proxy flips the fuse in memory from its `DllMain` | Keeps the executable byte-identical, but the proxy is only reachable when the loader searches the application directory |
| On disk | the patcher writes `r` into the fuse wire of `Wand.exe` | Always works, but changes one byte of a signed executable |

## Loader isolation

On hardened Windows 11 builds the loader resolves `VERSION.dll` from
`%SystemRoot%\System32` even when a copy sits next to the executable, so the
proxy never runs and the runtime bypass never happens. Check it in the
process module list:

```powershell
(Get-Process Wand).Modules | Where-Object ModuleName -eq 'VERSION.dll'
```

If the path is `C:\Windows\System32\VERSION.dll` instead of the install
folder, the proxy is inert and the fuse has to be removed on disk.

The patcher therefore removes the fuse in `Wand.exe` itself. The original
fuse value is stored next to the executable (`electron-fuse.wemod-enhancer-backup`)
and `restore` puts it back.

> The executable's Authenticode signature stops matching after the fuse is
> removed — one byte changes. The client still runs; only signature checks
> (and some scanners) notice. Pass `--no-fuse-flip` to keep the executable
> untouched and rely on the proxy alone.

## Re-patching after an update

A client update replaces the whole `app-<version>` folder, so the patch has
to be applied again to the new folder. The fuse removal is per executable, so
the new `Wand.exe` needs it too.
