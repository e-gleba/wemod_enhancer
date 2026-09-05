"""Patcher unit tests (stdlib only): regex sanity + ASAR round-trip.

Run:  python3 -m pytest tests/ -q
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

import wemod_enhancer as we

EXPECTED_FILES = ("index.js", "nested/app-1.bundle.js")


def _write_fixture(src_dir: Path) -> None:
    src_dir.mkdir()
    (src_dir / "index.js").write_text("console.log(1);", "utf-8")
    (src_dir / "nested").mkdir()
    (src_dir / "nested" / "app-1.bundle.js").write_text("var a=1;", "utf-8")


def _build_asar(src_dir: Path, asar: Path) -> None:
    header = {"files": {}}
    files = header["files"]
    offset = 0
    payload = b""
    for rel in EXPECTED_FILES:
        data = (src_dir / rel).read_bytes()
        node = files
        *dirs, leaf = rel.split("/")
        for d in dirs:
            node = node.setdefault(d, {"files": {}})["files"]
        node[leaf] = {"size": len(data), "offset": str(offset)}
        offset += len(data)
        payload += data
    raw = json.dumps(header, separators=(",", ":")).encode()
    with asar.open("wb") as f:
        hp = we.pickle(raw)
        f.write(we.size_pickle(len(hp)))
        f.write(hp)
        f.write(payload)


def test_markers_are_unique_per_patch():
    """No two patches share a marker (status stays truthful)."""
    markers = [p.marker for p in we.PATCHES if p.marker]
    assert len(markers) == len(set(markers)), "duplicate patch marker"
    for patch in we.PATCHES:
        assert patch.marker, patch.name


def test_required_patches_have_markers():
    """Every required patch declares a marker (idempotency + status)."""
    for patch in we.PATCHES:
        if not patch.optional:
            assert patch.marker, patch.name


def test_markers_match_their_replacement():
    """Each marker is a substring of its own replacement output."""
    samples = {
        "activate-pro-account": 'getUserAccount(){return this.#s.fetch({endpoint:"/v3/account" x})}',
        "activate-pro-language": 'setAccountLanguage("en"){return this.#a.post("/v3/account/language",{x:1})}',
        "activate-pro-brand": 'setAccountWandBrandExperience(){return this.#b.post("/v3/account/brand_experience_wand")}',
    }
    for patch in we.PATCHES:
        if patch.name in samples:
            src = samples[patch.name]
            out = re.sub(patch.patterns[0], patch.replacement, src, count=1, flags=re.S)
            assert patch.marker in out, patch.name
        elif patch.name == "activate-pro-reducer":
            out = patch.replacement
            assert isinstance(out, str) and patch.marker in out


def test_pro_account_rewrite():
    """getUserAccount fetch gains the subscription injection."""
    patch = next(p for p in we.PATCHES if p.name == "activate-pro-account")
    src = 'getUserAccount(){const x=1;return this.#s.fetch({endpoint:"/v3/account" Tebance})}'
    assert len(list(re.finditer(patch.patterns[0], src, re.S))) == 1
    out = re.sub(patch.patterns[0], patch.replacement, src, count=1, flags=re.S)
    assert 'period:"yearly"' in out and 'state:"active"' in out


def test_pro_reducer_compiles_and_rewrites():
    """Reducer pattern compiles (named backrefs) and rewrites."""
    patch = next(p for p in we.PATCHES if p.name == "activate-pro-reducer")
    compiled = [re.compile(p, re.S) for p in patch.patterns]
    assert compiled, "reducer must declare at least one pattern"
    src = 'account:((account)=>account&&"object"==typeof account?{...account}:account)(store.account)'
    assert sum(len(c.findall(src)) for c in compiled) == 1
    out = compiled[0].sub(patch.replacement, src, count=1)
    assert "{...account,subscription:" in out


def test_single_pro_patch_does_not_mark_all(tmp_path: Path):
    """One Pro rewrite must not flip every Pro marker to applied."""
    bundle = tmp_path / "index.js"
    patch = next(p for p in we.PATCHES if p.name == "activate-pro-account")
    src = 'getUserAccount(){return this.#s.fetch({endpoint:"/v3/account" x})}'
    bundle.write_text(re.sub(patch.patterns[0], patch.replacement, src, count=1, flags=re.S), "utf-8")
    found = we.scan_markers(tmp_path)
    assert found["activate-pro-account"] is True
    assert found["activate-pro-language"] is False
    assert found["activate-pro-reducer"] is False
    assert found["activate-pro-brand"] is False


def test_double_match_fails_closed(tmp_path: Path):
    """Two anchor hits = unsupported client, raise, change nothing."""
    (tmp_path / "index.js").write_text("a.whenReady().then(1);b.whenReady().then(2);", "utf-8")
    with pytest.raises(RuntimeError, match="devtools-f12"):
        we.patch_bundles(tmp_path, only={"devtools-f12"})


def test_safe_rejects_traversal(tmp_path: Path):
    """ASAR relpaths cannot escape the extraction root."""
    with pytest.raises(ValueError, match="unsafe ASAR path"):
        we.safe(tmp_path, "../evil.js")
    with pytest.raises(ValueError, match="unsafe ASAR path"):
        we.safe(tmp_path, "a/../../evil.js")


def test_pickle_roundtrip():
    """Chromium pickle keeps 4-byte alignment."""
    blob = we.pickle(b'{"a":1}')
    assert len(blob) % 4 == 0
    assert blob[:4] == (len(blob) - 4).to_bytes(4, "little")


def test_pe_x64_rejects_garbage(tmp_path: Path):
    """Truncated / non-PE dll is rejected before anything is copied."""
    bad = tmp_path / "version.dll"
    bad.write_bytes(b"MZ" + b"\0" * 10)
    with pytest.raises(RuntimeError):
        we.pe_x64(bad)


def test_dll_validated_before_backup(tmp_path: Path):
    """A bad --version-dll fails while the install is still pristine."""
    install = tmp_path / "app-1"
    resources = install / "resources"
    resources.mkdir(parents=True)
    asar = resources / "app.asar"
    src_dir = tmp_path / "src"
    _write_fixture(src_dir)
    _build_asar(src_dir, asar)
    before = asar.read_bytes()
    bad_dll = tmp_path / "version.dll"
    bad_dll.write_bytes(b"MZ" + b"\0" * 10)
    with pytest.raises(RuntimeError, match="version.dll"):
        we.do_patch(install, bad_dll, only={"devtools-f12"})
    assert asar.read_bytes() == before, "asar must be untouched on DLL failure"
    assert not (resources / "app.asar.backup").exists(), "no backup on pre-validation failure"


def test_asar_extract_pack_roundtrip(tmp_path: Path):
    """pack(extract(x)) preserves every payload byte and its metadata."""
    src_dir = tmp_path / "src"
    _write_fixture(src_dir)
    asar = tmp_path / "app.asar"
    _build_asar(src_dir, asar)

    out = tmp_path / "extracted"
    out.mkdir()
    parsed = we.extract_asar(asar, out)
    for rel in EXPECTED_FILES:
        assert (out / rel).read_bytes() == (src_dir / rel).read_bytes()

    repacked = tmp_path / "app2.asar"
    we.pack_asar(out, repacked, parsed)
    check, _ = we.read_asar(repacked)
    assert {rel for rel, _ in we.entries(check)} == set(EXPECTED_FILES)

    roundtrip = tmp_path / "roundtrip"
    roundtrip.mkdir()
    we.extract_asar(repacked, roundtrip)
    for rel in EXPECTED_FILES:
        assert (roundtrip / rel).read_bytes() == (src_dir / rel).read_bytes()
    for rel, meta in we.entries(check):
        original = (src_dir / rel).read_bytes()
        assert int(meta["size"]) == len(original)
        assert meta["integrity"]["hash"] == we.integrity(src_dir / rel)["hash"]


def test_unpacked_entries_sync_to_disk(tmp_path: Path):
    """Patched unpacked entries land in app.asar.unpacked, not just headers."""
    src_dir = tmp_path / "src"
    _write_fixture(src_dir)
    asar = tmp_path / "app.asar"
    _build_asar(src_dir, asar)
    live = Path(str(asar) + ".unpacked")
    (live / "nested").mkdir(parents=True)
    (live / "nested" / "app-1.bundle.js").write_bytes(b"old-bytes")

    out = tmp_path / "extracted"
    out.mkdir()
    parsed = dict(we.extract_asar(asar, out))
    # Mark one entry unpacked the way real Electron clients do for DLLs.
    node = parsed["files"]["nested"]["files"]["app-1.bundle.js"]
    node["unpacked"] = True
    (out / "nested" / "app-1.bundle.js").write_bytes(b"new-bytes")

    repacked = tmp_path / "app2.asar"
    we.pack_asar(out, repacked, parsed)
    published = Path(str(repacked) + ".unpacked") / "nested" / "app-1.bundle.js"
    assert published.read_bytes() == b"new-bytes"
