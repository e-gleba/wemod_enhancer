#!/usr/bin/env python3
"""WeMod Enhancer patcher: Pro unlock for the WeMod Electron client.

Installs version.dll and patches resources/app.asar in place.
Stdlib only, fail-safe (backup + all-or-nothing + one-command restore).

Usage:
    python wemod_enhancer.py patch [--install-dir DIR]
    python wemod_enhancer.py restore [--install-dir DIR]
    python wemod_enhancer.py status [--install-dir DIR]
    python wemod_enhancer.py doctor [--install-dir DIR]
    python wemod_enhancer.py list-patches
"""
from __future__ import annotations

import argparse
import hashlib
import json
import logging
import os
import platform
import re
import shutil
import struct
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Callable

__version__ = "1.0.7"

log = logging.getLogger("wemod_enhancer")

BLOCK_SIZE = 4 * 1024 * 1024
BACKUP_SUFFIX = ".backup"
DLL_BACKUP_SUFFIX = ".wemod-enhancer-backup"
PRO_MARKER = 'period:"yearly",state:"active"'

Replacement = str | Callable[[re.Match[str]], str]


@dataclass(frozen=True)
class Patch:
    """One JS bundle rewrite with one or more anchor patterns."""

    name: str
    description: str
    patterns: tuple[str, ...]
    replacement: Replacement
    optional: bool = False
    # Marker proving the patch already applied (idempotency / status).
    marker: str = ""


def _pro_account(m: re.Match[str]) -> str:
    name = m["s"]
    return (
        'getUserAccount(){return this.#' + name + '.fetch({endpoint:"/v3/account",'
        'method:"GET",name:"/v3/account",collectMetrics:0})'
        ".then(response=>{response.subscription={"
        + PRO_MARKER
        + "};return response;})}"
    )


def _pro_language(m: re.Match[str]) -> str:
    return (
        f'setAccountLanguage({m["p"]}){{return ({m["e"]})'
        '.then(response=>{response&&"object"==typeof response&&'
        "(response.subscription={" + PRO_MARKER + "});return response;})}"
    )


def _pro_brand(m: re.Match[str]) -> str:
    name = m["s"]
    return (
        "setAccountWandBrandExperience(){return this.#" + name + '.post("/v3/account/brand_experience_wand")'
        ".then(response=>{response.subscription={" + PRO_MARKER + "};return response;})}"
    )


def _devtools(m: re.Match[str]) -> str:
    app = m["a"]
    return (
        f'{app}.on("browser-window-created",((_,w)=>{{try{{w.webContents.on('
        '"before-input-event",((_,i)=>{if("F12"===i.key&&"keyDown"===i.type)'
        "{w.webContents.isDevToolsOpened()?w.webContents.closeDevTools():"
        'w.webContents.openDevTools({mode:"detach"})}}))}}catch(e){{}}}})),'
        f"{app}.whenReady().then("
    )


PATCHES: tuple[Patch, ...] = (
    Patch(
        name="activate-pro-account",
        description="Intercept /v3/account, inject yearly/active subscription.",
        patterns=(
            r"getUserAccount\(\)\{.*?return\s+this\.#(?P<s>[\w$]+)\.fetch\(\{.*?\}\)\}",
        ),
        replacement=_pro_account,
        marker=PRO_MARKER,
    ),
    Patch(
        name="activate-pro-language",
        description="Same subscription injection on setAccountLanguage().",
        patterns=(
            r"setAccountLanguage\((?P<p>[^)]*)\)\{\s*return\s+"
            r"(?P<e>this\.#\w+\.post\(\"/v3/account/language\",\{[^}]*\}\))\s*;?\s*\}",
        ),
        replacement=_pro_language,
        marker=PRO_MARKER,
    ),
    Patch(
        name="activate-pro-reducer",
        description="Reducer-level subscription override (newer clients).",
        patterns=(
            # Wand-Enhancer pro-account-reducer shape:
            # account:((account)=>account&&"object"==typeof account?{...}:account)(X)
            r"account:\(\((?P<v>\w+)\)=>\2&&\"object\"==typeof \2\?\{\.\.\.\2,"
            r"subscription:\{period:\"yearly\",state:\"active\"\}\}:\2\)\((?P<src>[^)]+)\)",
        ),
        replacement=(
            'account:((account)=>account&&"object"==typeof account'
            '?{...account,subscription:{period:"yearly",state:"active"}}:account)(\\g<src>)'
        ),
        optional=True,
        marker=PRO_MARKER,
    ),
    Patch(
        name="activate-pro-brand",
        description="Brand-experience endpoint subscription injection.",
        patterns=(
            r"setAccountWandBrandExperience\(\)\{.*?return\s+this\.#(?P<s>[\w$]+)"
            r"\.post\(\"/v3/account/brand_experience_wand\"\)\}",
        ),
        replacement=_pro_brand,
        optional=True,
        marker=PRO_MARKER,
    ),
    Patch(
        name="disable-native-pairing",
        description="Reject requestRemoteAuthCode() (no mobile-pairing nag).",
        patterns=(
            r"requestRemoteAuthCode\(\)\{return this\.#[\w$]+\.post\(\"/v3/auth/remote_code\"\)\}",
        ),
        replacement=(
            'requestRemoteAuthCode(){return Promise.reject('
            'new Error("wemod-enhancer: native mobile pairing disabled"))}'
        ),
        marker="wemod-enhancer: native mobile pairing disabled",
    ),
    Patch(
        name="disable-updates",
        description="ACTION_CHECK_FOR_UPDATE becomes a no-op.",
        patterns=(r"registerHandler\(\"ACTION_CHECK_FOR_UPDATE\".*?\)\)\)\)",),
        replacement='registerHandler("ACTION_CHECK_FOR_UPDATE",(e=>expectUpdateFeedUrl(e,(e=>null)))',
        marker="expectUpdateFeedUrl(e,(e=>null))",
    ),
    Patch(
        name="devtools-f12",
        description="F12 toggles detached DevTools on every window.",
        patterns=(r"(?P<a>\w+)\.whenReady\(\)\.then\(",),
        replacement=_devtools,
        marker="browser-window-created",
    ),
)


# ---------------------------------------------------------------- ASAR ---


def u32(data: bytes, pos: int) -> int:
    """Read one little-endian uint32. Raises struct.error when truncated."""
    return struct.unpack_from("<I", data, pos)[0]


def read_asar(path: Path) -> tuple[dict, int]:
    """Return (header, payload_offset) or raise ValueError on corruption."""
    with path.open("rb") as f:
        size_pickle = f.read(8)
        if len(size_pickle) != 8 or u32(size_pickle, 0) != 4:
            raise ValueError("invalid ASAR size pickle")
        header_size = u32(size_pickle, 4)
        header_pickle = f.read(header_size)
        if len(header_pickle) != header_size or header_size < 8:
            raise ValueError("invalid ASAR header pickle")
        payload_size, json_size = u32(header_pickle, 0), u32(header_pickle, 4)
        if payload_size + 4 != header_size or json_size > payload_size - 4:
            raise ValueError("invalid ASAR header sizes")
        header = json.loads(header_pickle[8 : 8 + json_size].decode("utf-8"))
    return header, 8 + header_size


def entries(node: dict, prefix: str = ""):
    """Yield (posix_relpath, meta) depth-first."""
    for name, item in node.get("files", {}).items():
        rel = f"{prefix}/{name}" if prefix else name
        if "files" in item:
            yield from entries(item, rel)
        else:
            yield rel, item


def safe(root: Path, rel: str) -> Path:
    """Resolve an ASAR relpath, rejecting traversal. Raises ValueError."""
    parts = PurePosixPath(rel).parts
    if not parts or any(x in ("", ".", "..") for x in parts):
        raise ValueError(f"unsafe ASAR path: {rel}")
    out = root.joinpath(*parts).resolve()
    if root.resolve() not in (out, *out.parents):
        raise ValueError(f"unsafe ASAR path: {rel}")
    return out


def extract_asar(asar: Path, out: Path) -> dict:
    """Unpack payload files into out/. Returns the header dict."""
    header, base = read_asar(asar)
    unpacked = Path(str(asar) + ".unpacked")
    with asar.open("rb") as f:
        for rel, meta in entries(header):
            if "link" in meta:
                continue
            dst = safe(out, rel)
            dst.parent.mkdir(parents=True, exist_ok=True)
            if meta.get("unpacked"):
                src = safe(unpacked, rel)
                if not src.is_file():
                    raise FileNotFoundError(f"missing unpacked ASAR file: {rel}")
                shutil.copy2(src, dst)
                continue
            f.seek(base + int(meta["offset"]))
            dst.write_bytes(f.read(int(meta["size"])))
    return header


def pickle(payload: bytes) -> bytes:
    """Chromium pickle of one value (uint32 len + data, 4-byte aligned)."""
    body = struct.pack("<I", len(payload)) + payload
    body += b"\0" * ((4 - len(body) % 4) % 4)
    return struct.pack("<I", len(body)) + body


def size_pickle(value: int) -> bytes:
    """ASAR outer size header: UInt32(4) + UInt32(value)."""
    return struct.pack("<II", 4, value)


def integrity(path: Path) -> dict:
    """Electron integrity block for one file (SHA256, 4 MiB blocks)."""
    whole = hashlib.sha256()
    blocks: list[str] = []
    with path.open("rb") as f:
        while block := f.read(BLOCK_SIZE):
            whole.update(block)
            blocks.append(hashlib.sha256(block).hexdigest())
    return {
        "algorithm": "SHA256",
        "hash": whole.hexdigest(),
        "blockSize": BLOCK_SIZE,
        "blocks": blocks,
    }


def pack_asar(root: Path, out: Path, header: dict) -> None:
    """Repack root/ into out, refresh sizes + integrity, verify size."""
    packed: list[Path] = []
    offset = 0
    for rel, meta in entries(header):
        if "link" in meta:
            continue
        src = safe(root, rel)
        if not src.is_file():
            raise FileNotFoundError(f"missing ASAR file: {rel}")
        meta["size"] = src.stat().st_size
        meta["integrity"] = integrity(src)
        if meta.get("unpacked"):
            meta.pop("offset", None)
            continue
        meta["offset"] = str(offset)
        packed.append(src)
        offset += meta["size"]
    raw = json.dumps(header, separators=(",", ":"), ensure_ascii=False).encode()
    header_pickle = pickle(raw)
    tmp = out.with_suffix(out.suffix + ".tmp")
    with tmp.open("wb") as f:
        f.write(size_pickle(len(header_pickle)))
        f.write(header_pickle)
        for src in packed:
            with src.open("rb") as item:
                shutil.copyfileobj(item, f, 1024 * 1024)
    check, data_offset = read_asar(tmp)
    expected = data_offset + sum(
        int(meta["size"])
        for _, meta in entries(check)
        if "link" not in meta and not meta.get("unpacked")
    )
    if tmp.stat().st_size != expected:
        raise RuntimeError("ASAR verification failed: archive size mismatch")
    tmp.replace(out)


# --------------------------------------------------------------- patches ---


def _bundle_files(root: Path) -> list[Path]:
    return [
        p
        for p in root.iterdir()
        if p.is_file()
        and (p.name == "index.js" or (p.name.startswith("app-") and p.name.endswith(".bundle.js")))
    ]


def patch_bundles(
    root: Path,
    only: set[str] | None = None,
    dry_run: bool = False,
) -> list[str]:
    """Apply PATCHES to bundles under root/. Returns applied patch names.

    Raises RuntimeError when a required patch has 0 or 2+ matches
    (fail closed on client drift). Already-patched files are skipped
    via marker detection, so patch is idempotent.
    """
    candidates = _bundle_files(root)
    if not candidates:
        raise RuntimeError(f"no JS bundles found in {root}")
    wanted = [p for p in PATCHES if only is None or p.name in only]
    if only is not None:
        unknown = only - {p.name for p in PATCHES}
        if unknown:
            raise ValueError(f"unknown patch: {sorted(unknown)}")
    pending: dict[str, Patch] = {p.name: p for p in wanted if not p.optional}
    optional: dict[str, Patch] = {p.name: p for p in wanted if p.optional}
    applied: list[str] = []

    for path in sorted(candidates):
        text = path.read_text("utf-8")
        changed = False
        for pool in (pending, optional):
            for name, patch in list(pool.items()):
                if patch.marker and patch.marker in text:
                    log.info("already applied %s: %s", name, path.name)
                    del pool[name]
                    if name not in applied:
                        applied.append(name)
                    continue
                matched = False
                for pattern in patch.patterns:
                    hits = list(re.finditer(pattern, text, re.S))
                    if not hits:
                        continue
                    if len(hits) != 1:
                        raise RuntimeError(f"{name}: expected one match, got {len(hits)}")
                    text = re.sub(pattern, patch.replacement, text, count=1, flags=re.S)
                    matched = True
                    break
                if matched:
                    changed = True
                    del pool[name]
                    applied.append(name)
                    log.info("%s %s: %s", "would patch" if dry_run else "patched", name, path.name)
        if changed and not dry_run:
            path.write_text(text, "utf-8")

    for name in optional:
        log.info("skipped optional patch %s: not present in this client", name)
    if pending:
        raise RuntimeError("unsupported client; missing patches: " + ", ".join(sorted(pending)))
    return applied


def scan_markers(root: Path) -> dict[str, bool]:
    """Check which patch markers are present in the unpacked bundles."""
    haystacks: list[str] = []
    for path in _bundle_files(root):
        try:
            haystacks.append(path.read_text("utf-8"))
        except OSError:
            continue
    haystack = "\n".join(haystacks)
    return {p.name: bool(p.marker and p.marker in haystack) for p in PATCHES}


# ----------------------------------------------------------------- paths ---


def pe_x64(path: Path) -> None:
    """Validate version.dll is a PE x86-64 image. Raises RuntimeError."""
    data = path.read_bytes()
    if len(data) < 0x40:
        raise RuntimeError("version.dll is truncated")
    off = u32(data, 0x3C)
    is_mz = data[:2] == b"MZ"
    is_pe = off + 6 <= len(data) and data[off : off + 4] == b"PE\0\0"
    is_x64 = is_pe and struct.unpack_from("<H", data, off + 4)[0] == 0x8664
    if not (is_mz and is_x64):
        raise RuntimeError("version.dll is not PE x86-64")


def find_bundled_dll() -> Path | None:
    """version.dll shipped next to this script (CMake install layout)."""
    dll = Path(__file__).resolve().parent / "version.dll"
    return dll if dll.is_file() else None


def resolve_dll(explicit: Path | None) -> Path:
    """Pick version.dll: explicit flag wins, else bundled copy."""
    if explicit is not None:
        return explicit
    bundled = find_bundled_dll()
    if bundled is not None:
        return bundled
    raise FileNotFoundError("version.dll not found next to script; pass --version-dll")


def newest_app_dir(root: Path) -> Path | None:
    """Newest app-x.y.z dir inside a WeMod root (numeric, not lexicographic)."""

    def key(p: Path) -> list[int]:
        parts: list[int] = []
        for tok in p.name.removeprefix("app-").split("."):
            try:
                parts.append(int(tok))
            except ValueError:
                parts.append(0)
        return parts

    apps = [p for p in root.iterdir() if p.is_dir() and p.name.startswith("app-")] if root.is_dir() else []
    return max(apps, key=key, default=None)


def find_install_dir() -> Path | None:
    """Auto-detect the WeMod install (newest app-* with app.asar)."""
    roots: list[Path] = []
    local = os.environ.get("LOCALAPPDATA", "")
    if local:
        roots.append(Path(local) / "WeMod")
    home = Path.home()
    roots += [
        home / "wemod-launcher" / "wemod_data" / "wemod_bin",
        home / "wemod-launcher",
        home / ".local" / "share" / "wemod-launcher" / "wemod_data" / "wemod_bin",
    ]
    for root in roots:
        if (root / "resources" / "app.asar").is_file():
            return root
        newest = newest_app_dir(root)
        if newest is not None and (newest / "resources" / "app.asar").is_file():
            return newest
    # Last resort: scan LOCALAPPDATA/WeMod/app-* directly.
    if local:
        newest = newest_app_dir(Path(local) / "WeMod")
        if newest is not None and (newest / "resources" / "app.asar").is_file():
            return newest
    return None


@dataclass
class Layout:
    """All on-disk paths for one install."""

    install: Path
    asar: Path
    backup: Path
    target_dll: Path
    dll_backup: Path


def layout_for(install: Path) -> Layout:
    """Derive asar/dll/backup paths from the install dir."""
    resources = install / "resources"
    return Layout(
        install=install,
        asar=resources / "app.asar",
        backup=resources / "app.asar.backup",
        target_dll=install / "version.dll",
        dll_backup=install / f"version.dll{DLL_BACKUP_SUFFIX}",
    )


def require_install(path: Path | None, action: str) -> Path:
    """Resolve --install-dir or auto-detect; raise with a fix-it message."""
    if path is not None:
        return path.resolve()
    found = find_install_dir()
    if found is not None:
        log.info("auto-detected install: %s", found)
        return found.resolve()
    raise FileNotFoundError(
        f"no WeMod install found for '{action}'. Pass --install-dir "
        "(Windows: %LOCALAPPDATA%\\WeMod\\app-<ver>; "
        "Linux: ~/wemod-launcher/wemod_data/wemod_bin after first login)."
    )


# --------------------------------------------------------------- actions ---


def do_patch(
    install: Path,
    dll: Path | None,
    *,
    only: set[str] | None = None,
    dry_run: bool = False,
) -> dict:
    """Patch one install. Returns a JSON-serializable report dict."""
    paths = layout_for(install)
    if not paths.asar.is_file():
        raise FileNotFoundError(f"missing {paths.asar}")
    if dry_run:
        with tempfile.TemporaryDirectory(prefix="wemod-enhancer-") as tmp:
            work = Path(tmp)
            header = extract_asar(paths.asar, work)
            applied = patch_bundles(work, only=only, dry_run=True)
        return {"install": str(install), "dry_run": True, "applied": applied}

    if not paths.backup.exists():
        shutil.copy2(paths.asar, paths.backup)
    else:
        shutil.copy2(paths.backup, paths.asar)
    if paths.target_dll.exists() and not paths.dll_backup.exists():
        shutil.copy2(paths.target_dll, paths.dll_backup)
    dll = resolve_dll(dll)
    pe_x64(dll)
    try:
        with tempfile.TemporaryDirectory(prefix="wemod-enhancer-") as tmp:
            work = Path(tmp)
            header = extract_asar(paths.asar, work)
            applied = patch_bundles(work, only=only)
            pack_asar(work, paths.asar, header)
        shutil.copy2(dll, paths.target_dll)
        log.info("patched %s", install)
    except Exception:
        shutil.copy2(paths.backup, paths.asar)
        if paths.dll_backup.exists():
            shutil.copy2(paths.dll_backup, paths.target_dll)
        elif paths.target_dll.exists():
            paths.target_dll.unlink()
        raise
    return {"install": str(install), "applied": applied, "backup": str(paths.backup)}


def do_restore(install: Path) -> dict:
    """Restore one install from its automatic backup."""
    paths = layout_for(install)
    if not paths.backup.exists():
        raise FileNotFoundError(f"missing {paths.backup}")
    shutil.copy2(paths.backup, paths.asar)
    if paths.dll_backup.exists():
        shutil.copy2(paths.dll_backup, paths.target_dll)
    elif paths.target_dll.exists():
        paths.target_dll.unlink()
    log.info("restored %s", install)
    return {"install": str(install), "restored": True}


def do_status(install: Path) -> dict:
    """Report patch state without touching anything."""
    paths = layout_for(install)
    report: dict = {
        "install": str(install),
        "asar": paths.asar.is_file(),
        "backup": paths.backup.exists(),
        "dll": paths.target_dll.is_file(),
        "dll_backup": paths.dll_backup.exists(),
        "patches": {},
    }
    if not paths.asar.is_file():
        return report
    with tempfile.TemporaryDirectory(prefix="wemod-enhancer-") as tmp:
        work = Path(tmp)
        try:
            extract_asar(paths.asar, work)
        except ValueError as exc:
            report["asar_error"] = str(exc)
            return report
        report["patches"] = scan_markers(work)
    return report


def do_doctor(install: Path | None) -> dict:
    """Environment + install health check. Never raises on bad installs."""
    info: dict = {
        "enhancer": __version__,
        "python": platform.python_version(),
        "python_ok": sys.version_info >= (3, 11),
        "os": platform.platform(),
        "bundled_dll": str(find_bundled_dll() or "<missing>"),
    }
    target = install.resolve() if install else find_install_dir()
    info["install"] = str(target) if target else "<not found>"
    if target is None:
        info["hint"] = "pass --install-dir or install WeMod first."
        return info
    paths = layout_for(target)
    info.update(
        {
            "asar": paths.asar.is_file(),
            "backup": paths.backup.exists(),
            "dll": paths.target_dll.is_file(),
            "writable": os.access(target, os.W_OK),
        }
    )
    try:
        read_asar(paths.asar)
        info["asar_valid"] = True
    except (OSError, ValueError) as exc:
        info["asar_valid"] = False
        info["asar_error"] = str(exc)
    try:
        dll = resolve_dll(None)
        pe_x64(dll)
        info["dll_ok"] = True
    except (OSError, RuntimeError, FileNotFoundError) as exc:
        info["dll_ok"] = False
        info["dll_error"] = str(exc)
    return info


# ------------------------------------------------------------------- cli ---


@dataclass
class Options:
    """Parsed CLI state (kept flat so the GUI one-liners keep working)."""

    command: str
    install_dir: Path | None = None
    version_dll: Path | None = None
    only: set[str] | None = None
    dry_run: bool = False
    as_json: bool = False
    verbose: bool = False


def build_parser() -> argparse.ArgumentParser:
    """Define the CLI. --install-dir is optional (auto-detect)."""
    parser = argparse.ArgumentParser(
        prog="wemod_enhancer",
        description="WeMod Enhancer: Pro unlock for the WeMod Electron client.",
    )
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    parser.add_argument("-v", "--verbose", action="store_true", help="debug logging.")
    parser.add_argument("--json", dest="as_json", action="store_true", help="machine-readable output.")
    sub = parser.add_subparsers(dest="command", required=True)

    for name in ("patch", "restore", "status", "doctor"):
        cmd = sub.add_parser(name, help=f"{name} the WeMod install.")
        cmd.add_argument("--install-dir", type=Path, default=None, help="app-x.y.z folder (auto-detected when omitted).")
        if name == "patch":
            cmd.add_argument("--version-dll", type=Path, default=None)
            cmd.add_argument("--dry-run", action="store_true", help="check only, change nothing.")
            cmd.add_argument("--only", nargs="+", default=None, metavar="PATCH", help="apply a subset (see list-patches).")
        if name in ("status", "doctor", "patch"):
            pass  # --json is global; kept here for discoverability in --help.

    sub.add_parser("list-patches", help="list available JS patches.")
    return parser


def main(argv: list[str] | None = None) -> int:
    """CLI entry point. Returns a process exit code."""
    args = build_parser().parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s: %(message)s",
    )

    if args.command == "list-patches":
        rows = [
            {"name": p.name, "optional": p.optional, "description": p.description}
            for p in PATCHES
        ]
        if args.as_json:
            print(json.dumps(rows, indent=2))
        else:
            for row in rows:
                tag = "(optional)" if row["optional"] else "(required)"
                print(f'{row["name"]:24} {tag:11} {row["description"]}')
        return 0

    if args.command == "doctor":
        install = args.install_dir.resolve() if args.install_dir else None
        report = do_doctor(install)
    elif args.command == "status":
        report = do_status(require_install(args.install_dir, "status"))
    elif args.command == "restore":
        report = do_restore(require_install(args.install_dir, "restore"))
    else:  # patch
        only = set(args.only) if args.only else None
        report = do_patch(
            require_install(args.install_dir, "patch"),
            args.version_dll.resolve() if args.version_dll else None,
            only=only,
            dry_run=args.dry_run,
        )

    if args.as_json:
        print(json.dumps(report, indent=2))
    elif args.command in ("status", "doctor"):
        for key, value in report.items():
            print(f"{key}: {value}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # fail loud: GUI streams this line into its log.
        log.error("error: %s", exc)
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
