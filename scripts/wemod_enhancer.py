#!/usr/bin/env python3
"""Install version.dll and patch Wand/WeMod Electron app.asar."""
from __future__ import annotations

import argparse, hashlib, json, mmap, re, shutil, struct, sys, tempfile
from pathlib import Path, PurePosixPath

BLOCK_SIZE = 4 * 1024 * 1024
PATCHES = {
 "activate-pro-account": (r'getUserAccount\(\)\{.*?return\s+this\.#(?P<s>[\w$]+)\.fetch\(\{.*?\}\)\}', lambda m: f'getUserAccount(){{return this.#{m["s"]}.fetch({{endpoint:"/v3/account",method:"GET",name:"/v3/account",collectMetrics:0}}).then(response=>{{response.subscription={{period:"yearly",state:"active"}};return response;}})}}'),
 "activate-pro-language": (r'setAccountLanguage\((?P<p>[^)]*)\)\{\s*return\s+(?P<e>this\.#\w+\.post\("/v3/account/language",\{[^}]*\}\))\s*;?\s*\}', lambda m: f'setAccountLanguage({m["p"]}){{return ({m["e"]}).then(response=>{{response&&"object"==typeof response&&(response.subscription={{period:"yearly",state:"active"}});return response;}})}}'),
 "disable-native-pairing": (r'requestRemoteAuthCode\(\)\{return this\.#[\w$]+\.post\("/v3/auth/remote_code"\)\}', 'requestRemoteAuthCode(){return Promise.reject(new Error("wemod-enhancer: native mobile pairing disabled"))}'),
 "disable-updates": (r'registerHandler\("ACTION_CHECK_FOR_UPDATE".*?\)\)\)\)', 'registerHandler("ACTION_CHECK_FOR_UPDATE",(e=>expectUpdateFeedUrl(e,(e=>null)))'),
 "devtools-f12": (r'(?P<a>\w+)\.whenReady\(\)\.then\(', lambda m: f'{m["a"]}.on("browser-window-created",((_,w)=>{{try{{w.webContents.on("before-input-event",((_,i)=>{{if("F12"===i.key&&"keyDown"===i.type){{w.webContents.isDevToolsOpened()?w.webContents.closeDevTools():w.webContents.openDevTools({{mode:"detach"}})}}}}))}}catch(e){{}}}})),{m["a"]}.whenReady().then('),
}
OPTIONAL_PATCHES = {
 "activate-pro-brand": (r'setAccountWandBrandExperience\(\)\{.*?return\s+this\.#(?P<s>[\w$]+)\.post\("/v3/account/brand_experience_wand"\)\}', lambda m: f'setAccountWandBrandExperience(){{return this.#{m["s"]}.post("/v3/account/brand_experience_wand").then(response=>{{response.subscription={{period:"yearly",state:"active"}};return response;}})}}'),
}

def u32(data: bytes, pos: int) -> int: return struct.unpack_from("<I", data, pos)[0]

def read_asar(path: Path) -> tuple[dict, int]:
    with path.open("rb") as f:
        size_pickle = f.read(8)
        if len(size_pickle) != 8 or u32(size_pickle, 0) != 4: raise ValueError("invalid ASAR size pickle")
        header_size = u32(size_pickle, 4)
        header_pickle = f.read(header_size)
        if len(header_pickle) != header_size or header_size < 8: raise ValueError("invalid ASAR header pickle")
        payload_size, json_size = u32(header_pickle, 0), u32(header_pickle, 4)
        if payload_size + 4 != header_size or json_size > payload_size - 4: raise ValueError("invalid ASAR header sizes")
        header = json.loads(header_pickle[8:8 + json_size].decode("utf-8"))
    return header, 8 + header_size

def entries(node: dict, prefix=""):
    for name, item in node.get("files", {}).items():
        rel = f"{prefix}/{name}" if prefix else name
        if "files" in item: yield from entries(item, rel)
        else: yield rel, item

def safe(root: Path, rel: str) -> Path:
    parts = PurePosixPath(rel).parts
    if not parts or any(x in ("", ".", "..") for x in parts): raise ValueError(f"unsafe ASAR path: {rel}")
    out = root.joinpath(*parts).resolve()
    if root.resolve() not in (out, *out.parents): raise ValueError(f"unsafe ASAR path: {rel}")
    return out

def extract_asar(asar: Path, out: Path):
    header, base = read_asar(asar); unpacked = Path(str(asar) + ".unpacked")
    with asar.open("rb") as f:
        for rel, meta in entries(header):
            if "link" in meta: continue
            dst = safe(out, rel); dst.parent.mkdir(parents=True, exist_ok=True)
            if meta.get("unpacked"):
                src = safe(unpacked, rel)
                if not src.is_file(): raise FileNotFoundError(f"missing unpacked ASAR file: {rel}")
                shutil.copy2(src, dst); continue
            f.seek(base + int(meta["offset"])); dst.write_bytes(f.read(int(meta["size"])))
    return header

def pickle(payload: bytes) -> bytes:
    body = struct.pack("<I", len(payload)) + payload
    body += b"\0" * ((4 - len(body) % 4) % 4)
    return struct.pack("<I", len(body)) + body

def size_pickle(value: int) -> bytes:
    return struct.pack("<II", 4, value)

def integrity(path: Path) -> dict:
    whole = hashlib.sha256(); blocks = []
    with path.open("rb") as f:
        while block := f.read(BLOCK_SIZE):
            whole.update(block); blocks.append(hashlib.sha256(block).hexdigest())
    return {"algorithm": "SHA256", "hash": whole.hexdigest(), "blockSize": BLOCK_SIZE, "blocks": blocks}

def pack_asar(root: Path, out: Path, header: dict):
    packed = []; offset = 0
    for rel, meta in entries(header):
        if "link" in meta: continue
        src = safe(root, rel)
        if not src.is_file(): raise FileNotFoundError(f"missing ASAR file: {rel}")
        meta["size"] = src.stat().st_size
        meta["integrity"] = integrity(src)
        if meta.get("unpacked"):
            meta.pop("offset", None); continue
        meta["offset"] = str(offset); packed.append(src); offset += meta["size"]
    raw = json.dumps(header, separators=(",", ":"), ensure_ascii=False).encode()
    header_pickle = pickle(raw)
    tmp = out.with_suffix(out.suffix + ".tmp")
    with tmp.open("wb") as f:
        f.write(size_pickle(len(header_pickle))); f.write(header_pickle)
        for src in packed:
            with src.open("rb") as item: shutil.copyfileobj(item, f, 1024 * 1024)
    check, data_offset = read_asar(tmp)
    expected = data_offset + sum(int(meta["size"]) for _, meta in entries(check) if "link" not in meta and not meta.get("unpacked"))
    if tmp.stat().st_size != expected: raise RuntimeError("ASAR verification failed: archive size mismatch")
    tmp.replace(out)

def patch_bundles(root: Path):
    candidates = [p for p in root.iterdir() if p.is_file() and (p.name == "index.js" or (p.name.startswith("app-") and p.name.endswith(".bundle.js")))]
    pending, optional = dict(PATCHES), dict(OPTIONAL_PATCHES)
    for path in candidates:
        text = path.read_text("utf-8"); changed = False
        for patches in (pending, optional):
            for name, (pattern, replacement) in list(patches.items()):
                matches = list(re.finditer(pattern, text, re.S))
                if not matches: continue
                if len(matches) != 1: raise RuntimeError(f"{name}: expected one match, got {len(matches)}")
                text = re.sub(pattern, replacement, text, count=1, flags=re.S); changed = True; del patches[name]
                print(f"patched {name}: {path.name}")
        if changed: path.write_text(text, "utf-8")
    for name in optional: print(f"skipped optional patch {name}: endpoint not present in this client")
    if pending: raise RuntimeError("unsupported client; missing patches: " + ", ".join(pending))

def pe_x64(path: Path):
    data = path.read_bytes()
    if len(data) < 0x40: raise RuntimeError("version.dll is truncated")
    off = u32(data, 0x3c)
    if off + 6 > len(data) or data[:2] != b"MZ" or data[off:off+4] != b"PE\0\0" or struct.unpack_from("<H", data, off+4)[0] != 0x8664: raise RuntimeError("version.dll is not PE x86-64")

def find_bundled_dll() -> Path | None:
    """Find version.dll next to this script (CMake install layout).

    After `cmake --install` both wemod_enhancer.py and version.dll
    land in the same directory.  This lets end users run the patcher
    with just Python -- no CMake or compiler needed.
    """
    dll = Path(__file__).resolve().parent / "version.dll"
    return dll if dll.is_file() else None

def resolve_dll(explicit: Path | None) -> Path:
    """Pick version.dll: explicit flag > bundled next to this script."""
    if explicit: return explicit
    bundled = find_bundled_dll()
    if bundled: return bundled
    raise FileNotFoundError("version.dll not found next to script; pass --version-dll")

def paths(install: Path):
    resources = install / "resources"
    return resources / "app.asar", resources / "app.asar.backup", install / "version.dll", install / "version.dll.wemod-enhancer-backup"

def electron_exe(install: Path) -> Path | None:
    """Electron executable of the install dir (Wand.exe / WeMod.exe)."""
    for name in ("Wand.exe", "WeMod.exe"):
        candidate = install / name
        if candidate.is_file(): return candidate
    return None

LOAD_CONFIG_DIRECTORY = 10
DEPENDENT_LOAD_FLAGS_OFFSET = 0x4E
LOAD_LIBRARY_SEARCH_SYSTEM32 = 0x0800

def u16(data: bytes, pos: int) -> int: return struct.unpack_from("<H", data, pos)[0]

def dependent_load_flags_offset(data) -> int | None:
    """File offset of DependentLoadFlags in a PE32+ load config, or None.

    Newer Wand builds set LOAD_LIBRARY_SEARCH_SYSTEM32 there, which
    restricts every statically imported DLL, version.dll included, to
    System32. The proxy next to the executable is then never loaded.
    Ref: https://learn.microsoft.com/en-us/cpp/build/reference/dependentloadflag
    """
    if len(data) < 0x40 or data[:2] != b"MZ": raise RuntimeError("electron executable is not a PE file")
    nt = u32(data, 0x3C)
    if nt + 24 > len(data) or data[nt:nt + 4] != b"PE\0\0": raise RuntimeError("electron executable has no PE header")
    optional = nt + 24; optional_size = u16(data, nt + 20)
    if optional + optional_size > len(data): raise RuntimeError("electron executable has a truncated optional header")
    if u16(data, optional) != 0x20B: return None
    entry = optional + 112 + LOAD_CONFIG_DIRECTORY * 8
    if entry + 8 > optional + optional_size: return None
    config_rva, config_size = struct.unpack_from("<II", data, entry)
    if config_rva == 0 or config_size < DEPENDENT_LOAD_FLAGS_OFFSET + 2: return None
    if config_rva < u32(data, optional + 60):
        config_offset = config_rva
    else:
        config_offset = None; sections = optional + optional_size
        for index in range(u16(data, nt + 6)):
            section = sections + index * 40
            if section + 40 > len(data): raise RuntimeError("electron executable has a truncated section table")
            virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, section + 8)
            if virtual_address <= config_rva < virtual_address + max(virtual_size, raw_size):
                delta = config_rva - virtual_address
                if delta >= raw_size: return None
                config_offset = raw_offset + delta; break
        if config_offset is None: return None
    flags_offset = config_offset + DEPENDENT_LOAD_FLAGS_OFFSET
    return flags_offset if flags_offset + 2 <= len(data) else None

def dependent_load_flags(exe: Path) -> int | None:
    """Current DependentLoadFlags of an executable, or None if not set."""
    with exe.open("rb") as f:
        if exe.stat().st_size == 0: return None
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
            offset = dependent_load_flags_offset(mm)
            return None if offset is None else u16(mm, offset)

def set_dependent_load_flags(exe: Path, flags: int) -> int:
    """Set DependentLoadFlags of an executable, return the previous value."""
    if not 0 <= flags <= 0xFFFF: raise ValueError(f"invalid DependentLoadFlags {flags!r}")
    with exe.open("r+b") as f:
        with mmap.mmap(f.fileno(), 0) as mm:
            offset = dependent_load_flags_offset(mm)
            if offset is None: raise RuntimeError(f"no load config directory in {exe}")
            previous = u16(mm, offset)
            if previous != flags: struct.pack_into("<H", mm, offset, flags); mm.flush()
            return previous

def patch(install: Path, dll: Path | None):
    asar, backup, target_dll, dll_backup = paths(install)
    if not asar.is_file(): raise FileNotFoundError(f"missing {asar}")
    if not backup.exists(): shutil.copy2(asar, backup)
    else: shutil.copy2(backup, asar)
    if target_dll.exists() and not dll_backup.exists(): shutil.copy2(target_dll, dll_backup)
    dll = resolve_dll(dll); pe_x64(dll)
    exe = electron_exe(install); load_flags_backup = install / "dependent-load-flags.wemod-enhancer-backup"
    load_flags_before = None; load_flags_backup_existed = load_flags_backup.exists()
    try:
        with tempfile.TemporaryDirectory(prefix="wemod-enhancer-") as tmp:
            work = Path(tmp); header = extract_asar(asar, work); patch_bundles(work); pack_asar(work, asar, header)
        shutil.copy2(dll, target_dll)
        if exe:
            flags = dependent_load_flags(exe)
            if flags is None: print(f"skipped dependent load flags: no load config in {exe.name}")
            elif not flags & LOAD_LIBRARY_SEARCH_SYSTEM32: print(f"dependent load flags already allow the app directory in {exe.name} (0x{flags:04X})")
            else:
                # Set before the write: if the setter fails partway, the
                # except path still knows the value to put back.
                load_flags_before = flags
                if not load_flags_backup.exists(): load_flags_backup.write_text(f"{flags:04x}", encoding="ascii")
                cleared = flags & ~LOAD_LIBRARY_SEARCH_SYSTEM32
                set_dependent_load_flags(exe, cleared)
                print(f"cleared LOAD_LIBRARY_SEARCH_SYSTEM32 in {exe.name} (DependentLoadFlags 0x{flags:04X} -> 0x{cleared:04X})")
        print(f"patched {install}")
    except Exception:
        shutil.copy2(backup, asar)
        if dll_backup.exists(): shutil.copy2(dll_backup, target_dll)
        elif target_dll.exists(): target_dll.unlink()
        if exe and load_flags_before is not None:
            try: set_dependent_load_flags(exe, load_flags_before)
            except Exception: pass
            if not load_flags_backup_existed and load_flags_backup.exists(): load_flags_backup.unlink()
        raise

def restore(install: Path):
    asar, backup, dll, dll_backup = paths(install)
    if not backup.exists(): raise FileNotFoundError(f"missing {backup}")
    shutil.copy2(backup, asar)
    if dll_backup.exists(): shutil.copy2(dll_backup, dll)
    elif dll.exists(): dll.unlink()
    exe = electron_exe(install); load_flags_backup = install / "dependent-load-flags.wemod-enhancer-backup"
    if load_flags_backup.exists():
        if exe:
            saved = load_flags_backup.read_text(encoding="ascii").strip()
            try: flags = int(saved, 16)
            except ValueError: raise RuntimeError(f"invalid dependent load flags backup: {saved!r}") from None
            if not 0 <= flags <= 0xFFFF: raise RuntimeError(f"invalid dependent load flags backup: {saved!r}")
            current = dependent_load_flags(exe)
            if current is not None and current != flags:
                set_dependent_load_flags(exe, flags); print(f"restored DependentLoadFlags in {exe.name} (0x{current:04X} -> 0x{flags:04X})")
        load_flags_backup.unlink()
    print(f"restored {install}")

def main():
    p = argparse.ArgumentParser(); sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("patch", "restore"):
        q = sub.add_parser(name); q.add_argument("--install-dir", type=Path, required=True)
        if name == "patch": q.add_argument("--version-dll", type=Path)
    a = p.parse_args()
    if a.cmd == "patch": patch(a.install_dir.resolve(), a.version_dll.resolve() if a.version_dll else None)
    else: restore(a.install_dir.resolve())

if __name__ == "__main__":
    try: main()
    except Exception as e: print(f"error: {e}", file=sys.stderr); raise SystemExit(1)
