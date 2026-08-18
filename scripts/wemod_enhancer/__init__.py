#!/usr/bin/env python3
"""Build/install version.dll and patch Wand/WeMod Electron app.asar."""
from __future__ import annotations

import argparse, hashlib, json, os, re, shutil, struct, subprocess, sys, tempfile
from pathlib import Path, PurePosixPath

# Repo root from a source checkout: scripts/wemod_enhancer/__init__.py
ROOT = Path(__file__).resolve().parents[2]
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

def find_dll(build: Path) -> Path:
    found = list(build.rglob("version.dll"))
    if not found: raise RuntimeError("build completed without version.dll")
    return found[0]

def find_bundled_dll() -> Path | None:
    """Find a prebuilt version.dll shipped next to this file.

    Wheel layout:  site-packages/wemod_enhancer/version.dll (package dir).
    CMake layout:  <prefix>/bin/version.dll next to wemod_enhancer.py.
    """
    here = Path(__file__).resolve().parent
    for candidate in (here / "version.dll", here.parent / "version.dll"):
        if candidate.is_file():
            return candidate
    return None

def build_dll() -> Path:
    if not (ROOT / "CMakeLists.txt").is_file():
        raise RuntimeError(
            "build-dll needs a source checkout; install the release wheel "
            "for a prebuilt version.dll")
    build = ROOT / "build" / "proxy"
    args = ["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"]
    if os.name != "nt": args += ["-DCMAKE_TOOLCHAIN_FILE=" + str(ROOT / "cmake/toolchains/llvm_mingw.cmake"), "-DCMAKE_SYSTEM_PROCESSOR=x86_64"]
    subprocess.run(args, check=True); subprocess.run(["cmake", "--build", str(build), "--config", "Release"], check=True)
    dll = find_dll(build); pe_x64(dll); return dll

def resolve_dll(explicit: Path | None) -> Path:
    """Pick version.dll: explicit flag > bundled > build from source."""
    if explicit: return explicit
    bundled = find_bundled_dll()
    if bundled: return bundled
    return build_dll()

def paths(install: Path):
    resources = install / "resources"
    return resources / "app.asar", resources / "app.asar.backup", install / "version.dll", install / "version.dll.wemod-enhancer-backup"

def patch(install: Path, dll: Path | None):
    asar, backup, target_dll, dll_backup = paths(install)
    if not asar.is_file(): raise FileNotFoundError(f"missing {asar}")
    if not backup.exists(): shutil.copy2(asar, backup)
    else: shutil.copy2(backup, asar)
    if target_dll.exists() and not dll_backup.exists(): shutil.copy2(target_dll, dll_backup)
    dll = resolve_dll(dll); pe_x64(dll)
    try:
        with tempfile.TemporaryDirectory(prefix="wemod-enhancer-") as tmp:
            work = Path(tmp); header = extract_asar(asar, work); patch_bundles(work); pack_asar(work, asar, header)
        shutil.copy2(dll, target_dll); print(f"patched {install}")
    except Exception:
        shutil.copy2(backup, asar)
        if dll_backup.exists(): shutil.copy2(dll_backup, target_dll)
        elif target_dll.exists(): target_dll.unlink()
        raise

def restore(install: Path):
    asar, backup, dll, dll_backup = paths(install)
    if not backup.exists(): raise FileNotFoundError(f"missing {backup}")
    shutil.copy2(backup, asar)
    if dll_backup.exists(): shutil.copy2(dll_backup, dll)
    elif dll.exists(): dll.unlink()
    print(f"restored {install}")

def main():
    p = argparse.ArgumentParser(); sub = p.add_subparsers(dest="cmd", required=True)
    for name in ("patch", "restore"):
        q = sub.add_parser(name); q.add_argument("--install-dir", type=Path, required=True)
        if name == "patch": q.add_argument("--version-dll", type=Path)
    sub.add_parser("build-dll"); a = p.parse_args()
    if a.cmd == "build-dll": print(build_dll())
    elif a.cmd == "patch": patch(a.install_dir.resolve(), a.version_dll.resolve() if a.version_dll else None)
    else: restore(a.install_dir.resolve())

def cli() -> None:
    """Console entry point: turn tracebacks into a one-line error + exit 1."""
    try:
        main()
    except Exception as e:
        print(f"error: {e}", file=sys.stderr)
        raise SystemExit(1)

if __name__ == "__main__":
    cli()
