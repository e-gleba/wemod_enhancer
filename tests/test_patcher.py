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


def test_required_patches_have_markers():
    """Every required patch declares a marker (idempotency + status)."""
    for patch in we.PATCHES:
        if not patch.optional:
            assert patch.marker, patch.name


def test_pro_account_rewrite():
    """getUserAccount fetch gains the subscription injection."""
    patch = next(p for p in we.PATCHES if p.name == "activate-pro-account")
    src = 'getUserAccount(){const x=1;return this.#s.fetch({endpoint:"/v3/account" Tebance})}'
    assert len(list(re.finditer(patch.patterns[0], src, re.S))) == 1
    out = re.sub(patch.patterns[0], patch.replacement, src, count=1, flags=re.S)
    assert 'period:"yearly"' in out and 'state:"active"' in out


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


def test_asar_extract_pack_roundtrip(tmp_path: Path):
    """pack(extract(x)) preserves payload bytes and stays verifiable."""
    src_dir = tmp_path / "src"
    src_dir.mkdir()
    (src_dir / "index.js").write_text("console.log(1);", "utf-8")
    (src_dir / "nested").mkdir()
    (src_dir / "nested" / "app-1.bundle.js").write_text("var a=1;", "utf-8")

    header = {"files": {}}
    files = header["files"]
    offset = 0
    payload = b""
    for rel in ("index.js", "nested/app-1.bundle.js"):
        data = (src_dir / rel).read_bytes()
        node = files
        *dirs, leaf = rel.split("/")
        for d in dirs:
            node = node.setdefault(d, {"files": {}})["files"]
        node[leaf] = {"size": len(data), "offset": str(offset)}
        offset += len(data)
        payload += data
    raw = json.dumps(header, separators=(",", ":")).encode()
    asar = tmp_path / "app.asar"
    with asar.open("wb") as f:
        hp = we.pickle(raw)
        f.write(we.size_pickle(len(hp)))
        f.write(hp)
        f.write(payload)

    out = tmp_path / "extracted"
    out.mkdir()
    parsed = we.extract_asar(asar, out)
    assert (out / "index.js").read_text() == "console.log(1);"

    repacked = tmp_path / "app2.asar"
    we.pack_asar(out, repacked, parsed)
    check, _ = we.read_asar(repacked)
    names = {rel for rel, _ in we.entries(check)}
    assert names == {"index.js", "nested/app-1.bundle.js"}
