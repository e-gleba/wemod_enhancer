# wemod_enhancer

Cross-platform Wand/WeMod Electron patcher: Pro subscription active,
auto-updates disabled, F12 DevTools, no mobile-pairing nag. Stdlib-only
Python 3.11+; the ASAR-integrity bypass lives in a 4 KB `version.dll`
proxy written in C.

## Install

### Release wheel — recommended (bundles the prebuilt `version.dll`)

```sh
pip install https://github.com/e-gleba/wemod_enhancer/releases/download/v1.0.2/wemod_enhancer-1.0.2-py3-none-any.whl
```

Check the [latest release](https://github.com/e-gleba/wemod_enhancer/releases/latest)
for the current wheel filename. `pipx install <url>` and
`uv tool install <url>` work too.

### Straight from the repository

```sh
pipx install "git+https://github.com/e-gleba/wemod_enhancer.git#subdirectory=scripts"
```

Source installs ship no DLL — `patch` builds it with CMake on first use
(or pass `--version-dll /path/to/version.dll`).

## Usage

```sh
wemod-enhancer patch --install-dir <WeMod install>
wemod-enhancer restore --install-dir <WeMod install>
wemod-enhancer build-dll            # source checkouts only
```

`python -m wemod_enhancer ...` is equivalent. In the CMake install tree
the package sits next to the DLL: `python bin/wemod_enhancer patch ...`.
