#!/usr/bin/env python3
"""Build/install version.dll and patch Wand/WeMod Electron app.asar.

Python 3.11+; standard library only. Works on Windows and Linux/macOS hosts.
ASAR implementation supports regular files and existing app.asar.unpacked files.
"""
from __future__ import annotations

import argparse, json, os, re, shutil, struct, subprocess, sys, tempfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
EXPORTS = {"GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA", "GetFileVersionInfoExW", "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA", "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW", "GetFileVersionInfoW", "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW", "VerLanguageNameA", "VerLanguageNameW", "VerQueryValueA", "VerQueryValueW"}

PATCHES = {
 "activate-pro-account": (r'getUserAccount\(\)\{.*?return\s+this\.#(?P<s>[\w$]+)\.fetch\(\{.*?\}\)\}', lambda m: f'getUserAccount(){{return this.#{m["s"]}.fetch({{endpoint:"/v3/account",method:"GET",name:"/v3/account",collectMetrics:0}}).then(response=>{{response.subscription={{period:"yearly",state:"active"}};return response;}})}}'),
 "activate-pro-brand": (r'setAccountWandBrandExperience\(\)\{.*?return\s+this\.#(?P<s>[\w$]+)\.post\("/v3/account/brand_experience_wand"\)\}', lambda m: f'setAccountWandBrandExperience(){{return this.#{m["s"]}.post("/v3/account/brand_experience_wand").then(response=>{{response.subscription={{period:"yearly",state:"active"}};return response;}})}}'),
 "activate-pro-language": (r'setAccountLanguage\((?P<p>[^)]*)\)\{\s*return\s+(?P<e>this\.#\w+\.post\("/v3/account/language",\{[^}]*\}\))\s*;?\s*\}', lambda m: f'setAccountLanguage({m["p"]}){{return ({m["e"]}).then(response=>{{response&&"object"==typeof response&&(response.subscription={{period:"yearly",state:"active"}});return response;}})}}'),
 "disable-native-pairing": (r'requestRemoteAuthCode\(\)\{return this\.#[\w$]+\.post\("/v3/auth/remote_code"\)\}', 'requestRemoteAuthCode(){return Promise.reject(new Error("wemod-enhancer: native mobile pairing disabled"))}'),
 "disable-updates": (r'registerHandler\("ACTION_CHECK_FOR_UPDATE".*?\)\)\)\)', 'registerHandler("ACTION_CHECK_FOR_UPDATE",(e=>expectUpdateFeedUrl(e,(e=>null)))'),
 "devtools-f12": (r'(?P<a>\w+)\.whenReady\(\)\.then\(', lambda m: f'{m["a"]}.on("browser-window-created",((_,w)=>{{try{{w.webContents.on("before-input-event",((_,i)=>{{if("F12"===i.key&&"keyDown"===i.type){{w.webContents.isDevToolsOpened()?w.webContents.closeDevTools():w.webContents.openDevTools({{mode:"detach"}})}}}}))}}catch(e){{}}}})),{m["a"]}.whenReady().then('),
}

def u32(data: bytes, pos: int) -> int: return struct.unpack_from("<I", data, pos)[0]

def read_asar(path: Path) -> tuple[dict, int]:
    with path.open("rb") as f:
        head = f.read(16)
        if len(head) < 16: raise ValueError("invalid ASAR header")
        header_size = u32(head, 4)
        json_size = u32(head, 12)
        f.seek(16)
        header = json.loads(f.read(json_size).decode("utf-8"))
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
            dst = safe(out, rel); dst.parent.mkdir(parents=True, exist_ok=True)
            if meta.get("unpacked"):
                src = safe(unpacked, rel)
                if src.exists(): shutil.copy2(src, dst)
                continue
            f.seek(base + int(meta.get("offset", 0))); dst.write_bytes(f.read(int(meta.get("size", 0))))

def pickle(payload: bytes) -> bytes:
    body = struct.pack("<I", len(payload)) + payload
    body += b"\0" * ((4 - len(body) % 4) % 4)
    return struct.pack("<I", len(body)) + body

def pack_asar(root: Path, out: Path):
    files = sorted(p for p in root.rglob("*") if p.is_file())
    tree = {"files": {}}; offset = 0
    for p in files:
        rel = p.relative_to(root).as_posix(); node = tree
        for part in PurePosixPath(rel).parts[:-1]: node = node["files"].setdefault(part, {"files": {}})
        size = p.stat().st_size; node["files"][p.name] = {"size": size, "offset": str(offset)}; offset += size
    raw = json.dumps(tree, separators=(",", ":"), ensure_ascii=False).encode()
    header = pickle(raw); size = pickle(struct.pack("<I", len(header)))
    tmp = out.with_suffix(out.suffix + ".tmp")
    with tmp.open("wb") as f:
        f.write(size); f.write(header)
        for p in files: f.write(p.read_bytes())
    tmp.replace(out)

def patch_bundles(root: Path):
    candidates = [p for p in root.iterdir() if p.is_file() and (p.name == "index.js" or (p.name.startswith("app-") and p.name.endswith(".bundle.js")))]
    pending = dict(PATCHES)
    for path in candidates:
        text = path.read_text("utf-8"); changed = False
        for name, (pattern, replacement) in list(pending.items()):
            matches = list(re.finditer(pattern, text, re.S))
            if not matches: continue
            if len(matches) != 1: raise RuntimeError(f"{name}: expected one match, got {len(matches)}")
            text = re.sub(pattern, replacement, text, count=1, flags=re.S); changed = True; del pending[name]
            print(f"patched {name}: {path.name}")
        if changed: path.write_text(text, "utf-8")
    if pending: raise RuntimeError("unsupported client; missing patches: " + ", ".join(pending))

def pe_x64(path: Path):
    data = path.read_bytes(); off = u32(data, 0x3c)
    if data[:2] != b"MZ" or data[off:off+4] != b"PE\0\0" or struct.unpack_from("<H", data, off+4)[0] != 0x8664: raise RuntimeError("version.dll is not PE x86-64")

def find_dll(build: Path) -> Path:
    found = list(build.rglob("version.dll"))
    if not found: raise RuntimeError("build completed without version.dll")
    return found[0]

def build_dll() -> Path:
    build = ROOT / "build" / "proxy"
    args = ["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF"]
    if os.name != "nt": args += ["-DCMAKE_TOOLCHAIN_FILE=" + str(ROOT / "cmake/toolchains/llvm_mingw.cmake"), "-DCMAKE_SYSTEM_PROCESSOR=x86_64"]
    subprocess.run(args, check=True); subprocess.run(["cmake", "--build", str(build), "--config", "Release"], check=True)
    dll = find_dll(build); pe_x64(dll); return dll

def paths(install: Path):
    resources = install / "resources"
    return resources / "app.asar", resources / "app.asar.backup", install / "version.dll", install / "version.dll.wemod-enhancer-backup"

def patch(install: Path, dll: Path | None):
    asar, backup, target_dll, dll_backup = paths(install)
    if not asar.is_file(): raise FileNotFoundError(f"missing {asar}")
    if not backup.exists(): shutil.copy2(asar, backup)
    else: shutil.copy2(backup, asar)
    if target_dll.exists() and not dll_backup.exists(): shutil.copy2(target_dll, dll_backup)
    dll = dll or build_dll(); pe_x64(dll)
    with tempfile.TemporaryDirectory(prefix="wemod-enhancer-") as tmp:
        work = Path(tmp); extract_asar(asar, work); patch_bundles(work); pack_asar(work, asar)
    shutil.copy2(dll, target_dll); print(f"patched {install}")

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
    sub.add_parser("build-dll")
    a = p.parse_args()
    if a.cmd == "build-dll": print(build_dll())
    elif a.cmd == "patch": patch(a.install_dir.resolve(), a.version_dll.resolve() if a.version_dll else None)
    else: restore(a.install_dir.resolve())

if __name__ == "__main__":
    try: main()
    except Exception as e: print(f"error: {e}", file=sys.stderr); raise SystemExit(1)
