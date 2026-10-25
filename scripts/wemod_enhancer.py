#!/usr/bin/env python3
"""Patch Wand/WeMod Electron app.asar and disable ASAR integrity fuse."""
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

FUSE_SENTINEL = b"dL7pKGdnNz796PbbjQWNKmHXBZaB9tsX"
FUSE_EMBEDDED_ASAR_INTEGRITY = 4
FUSE_OFF, FUSE_ON, FUSE_REMOVED = "0", "1", "r"

def find_fuse_wire(data: bytes) -> int:
    """Offset of the Electron fuse wire inside an executable, or -1.

    The wire is `sentinel[32] + version(1) + length(1) + fuses[length]`.
    """
    start = 0
    while True:
        off = data.find(FUSE_SENTINEL, start)
        if off < 0: return -1
        if off + 34 <= len(data) and data[off + 32] == 1 and data[off + 33] >= 5: return off
        start = off + 1

def fuse_state(exe: Path) -> str | None:
    """Current value of the embedded-ASAR-integrity fuse, or None if absent."""
    with exe.open("rb") as f:
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
            off = find_fuse_wire(mm)
            return None if off < 0 else chr(mm[off + 34 + FUSE_EMBEDDED_ASAR_INTEGRITY])

def set_fuse_state(exe: Path, state: str) -> str:
    """Set the embedded-ASAR-integrity fuse, return the previous value."""
    if state not in (FUSE_OFF, FUSE_ON, FUSE_REMOVED): raise ValueError(f"invalid fuse state {state!r}")
    with exe.open("r+b") as f:
        with mmap.mmap(f.fileno(), 0) as mm:
            off = find_fuse_wire(mm)
            if off < 0: raise RuntimeError(f"electron fuse wire not found in {exe}")
            pos = off + 34 + FUSE_EMBEDDED_ASAR_INTEGRITY
            previous = chr(mm[pos])
            if previous not in (FUSE_OFF, FUSE_ON, FUSE_REMOVED): raise RuntimeError(f"unexpected fuse value {previous!r} in {exe}")
            if previous == state: return previous
            mm[pos] = ord(state); mm.flush()
            return previous

def electron_exe(install: Path) -> Path | None:
    """Electron executable of the install dir (Wand.exe / WeMod.exe)."""
    for name in ("Wand.exe", "WeMod.exe"):
        candidate = install / name
        if candidate.is_file(): return candidate
    return None

def exe_backup_path(exe: Path) -> Path:
    return exe.with_name(exe.name + ".wemod-enhancer-backup")

def cleanup_legacy_dll(install: Path):
    """Remove the retired version.dll proxy, restoring any pre-proxy file."""
    dll = install / "version.dll"
    dll_backup = install / "version.dll.wemod-enhancer-backup"
    if dll_backup.is_file():
        shutil.copy2(dll_backup, dll)
        dll_backup.unlink()
        print(f"restored pre-proxy {dll.name}")
        return
    if dll.is_file():
        print(f"kept unexpected {dll.name} - remove manually if it is the retired proxy")
        return

def flip_fuse(exe: Path):
    backup = exe_backup_path(exe)
    previous = fuse_state(exe)
    if previous is None:
        print(f"skipped fuse flip: no electron fuse wire in {exe.name}")
        return
    if previous == FUSE_REMOVED:
        print(f"asar integrity fuse already removed in {exe.name}")
        return
    shutil.copy2(exe, backup)
    set_fuse_state(exe, FUSE_REMOVED)
    print(f"removed asar integrity fuse in {exe.name} (was '{previous}')")

def restore_fuse(exe: Path):
    backup = exe_backup_path(exe)
    legacy_sidecar = exe.parent / "electron-fuse.wemod-enhancer-backup"
    if backup.is_file():
        shutil.copy2(backup, exe)
        backup.unlink()
        print(f"restored {exe.name} from backup")
        return
    if legacy_sidecar.is_file():
        previous = legacy_sidecar.read_text(encoding="ascii").strip()
        if previous and fuse_state(exe) == FUSE_REMOVED:
            set_fuse_state(exe, previous)
            print(f"restored asar integrity fuse in {exe.name} (was '{previous}')")
        legacy_sidecar.unlink()

def paths(install: Path):
    resources = install / "resources"
    return resources / "app.asar", resources / "app.asar.backup"

def patch(install: Path):
    asar, backup = paths(install)
    if not asar.is_file(): raise FileNotFoundError(f"missing {asar}")
    if not backup.exists(): shutil.copy2(asar, backup)
    else: shutil.copy2(backup, asar)
    exe = electron_exe(install)
    exe_touched = False
    try:
        with tempfile.TemporaryDirectory(prefix="wemod-enhancer-") as tmp:
            work = Path(tmp); header = extract_asar(asar, work); patch_bundles(work); pack_asar(work, asar, header)
        cleanup_legacy_dll(install)
        if exe: flip_fuse(exe); exe_touched = True
        else: print("skipped fuse flip: no Wand.exe/WeMod.exe next to resources")
        print(f"patched {install}")
    except Exception:
        shutil.copy2(backup, asar)
        if exe and exe_touched: restore_fuse(exe)
        raise

def restore(install: Path):
    asar, backup = paths(install)
    if not backup.exists(): raise FileNotFoundError(f"missing {backup}")
    shutil.copy2(backup, asar)
    cleanup_legacy_dll(install)
    exe = electron_exe(install)
    if exe: restore_fuse(exe)
    print(f"restored {install}")

def main():
    p = argparse.ArgumentParser(); sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("patch", "restore"):
        q = sub.add_parser(name); q.add_argument("--install-dir", type=Path, required=True)
    a = p.parse_args()
    if a.cmd == "patch": patch(a.install_dir.resolve())
    else: restore(a.install_dir.resolve())

if __name__ == "__main__":
    try: main()
    except Exception as e: print(f"error: {e}", file=sys.stderr); raise SystemExit(1)
