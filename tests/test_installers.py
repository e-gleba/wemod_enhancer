"""One-liner installer tests (stdlib only): static checks, no network.

Covers scripts/install.ps1 + scripts/install.sh so the copy-paste
one-liners in the readme can never rot: URLs, actions, safety flags.
PowerShell syntax is validated with pwsh when available (skipped in CI
otherwise); bash syntax is validated with `bash -n` unconditionally.

Run:  python3 -m pytest tests/ -q
"""
from __future__ import annotations

import re
import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[1]
PS1 = REPO / "scripts" / "install.ps1"
SH = REPO / "scripts" / "install.sh"

PS1_URL = "https://raw.githubusercontent.com/e-gleba/wemod_enhancer/main/scripts/install.ps1"
SH_URL = "https://raw.githubusercontent.com/e-gleba/wemod_enhancer/main/scripts/install.sh"


def test_scripts_exist_with_shebang():
    """Both one-liners exist; the shell one declares bash (+x is a CI job)."""
    assert PS1.is_file() and SH.is_file()
    assert SH.read_text().startswith("#!/usr/bin/env bash")


def test_sh_bash_syntax():
    """`bash -n` parses install.sh (catches quoting/case breakage)."""
    proc = subprocess.run(["bash", "-n", str(SH)], capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr


def test_sh_shellcheck_clean():
    """shellcheck warning-free when the binary exists (CI runs it too)."""
    if shutil.which("shellcheck") is None:
        pytest.skip("shellcheck not installed")
    proc = subprocess.run(
        ["shellcheck", "-S", "warning", str(SH)],
        capture_output=True,
        text=True,
    )
    assert proc.returncode == 0, proc.stdout


@pytest.mark.skipif(shutil.which("pwsh") is None, reason="pwsh not installed")
def test_ps1_powershell_syntax():
    """PowerShell parser reads install.ps1 (catches brace/quote drift)."""
    script = (
        "$errs=$null;$toks=$null;"
        "[void][System.Management.Automation.Language.Parser]::ParseFile("
        f"'{PS1}',[ref]$toks,[ref]$errs);"
        "if($errs.Count){$errs|%{$_.Message};exit 1}"
    )
    proc = subprocess.run(["pwsh", "-NoProfile", "-Command", script], capture_output=True, text=True)
    assert proc.returncode == 0, proc.stdout + proc.stderr


def test_ps1_psscriptanalyzer_clean():
    """PSScriptAnalyzer error-free when the module exists."""
    if shutil.which("pwsh") is None:
        pytest.skip("pwsh not installed")
    probe = subprocess.run(
        ["pwsh", "-NoProfile", "-Command", "Get-Module -ListAvailable PSScriptAnalyzer"],
        capture_output=True,
        text=True,
    )
    if "PSScriptAnalyzer" not in probe.stdout:
        pytest.skip("PSScriptAnalyzer not installed")
    proc = subprocess.run(
        [
            "pwsh",
            "-NoProfile",
            "-Command",
            f"Invoke-ScriptAnalyzer -Path '{PS1}' -Severity Error | %{{$_.Message}}; exit 0",
        ],
        capture_output=True,
        text=True,
    )
    assert proc.stdout.strip() == "", proc.stdout


def test_ps1_one_liner_shape():
    """The irm|iex line from the readme exists verbatim in the header."""
    text = PS1.read_text()
    assert f"irm {PS1_URL} | iex" in text
    assert "#Requires -Version 5.1" in text
    assert "$ErrorActionPreference = 'Stop'" in text
    assert "ValidateSet('patch', 'restore')" in text


def test_sh_one_liner_shape():
    """The curl|bash lines from the readme exist verbatim in the header."""
    text = SH.read_text()
    assert f"curl -fsSL {SH_URL} | bash" in text
    assert "curl -fsSL .../install.sh | bash -s -- restore" in text
    assert "set -euo pipefail" in text


def test_installers_never_stack_patches():
    """Both scripts document the backup-first re-run guarantee."""
    assert "never stacks changes" in PS1.read_text()
    assert "never stacks changes" in SH.read_text()


def test_installers_pin_releases():
    """Version pinning works on both sides (reproducible installs)."""
    ps1, sh = PS1.read_text(), SH.read_text()
    assert "-Version v1.0.7" in ps1  # documented variant
    assert "releases/download/$Version/$PackageName" in ps1
    assert "PACKAGE_URL=" in sh and "releases/latest/download" in sh


def test_ps1_enforces_python_floor():
    """install.ps1 accepts 3.11+ only and rechecks after winget."""
    text = PS1.read_text()
    assert "$MinPython" in text and "3.11.0" in text
    assert "Test-PythonVersion" in text
    assert "still missing after install" in text


def test_installers_fail_with_fixits():
    """Missing WeMod / tools produce copy-pasteable next steps."""
    ps1 = PS1.read_text()
    assert "https://www.wemod.com/download" in ps1
    sh = SH.read_text()
    assert "wemod_bin appears after first login" in sh
    assert "preinstalled on SteamOS" in sh


def test_completion_messages_match_action():
    """Only patch claims Pro; restore/status/doctor get their own lines."""
    assert "Original WeMod files are back" in PS1.read_text()
    sh = SH.read_text()
    assert "Launch WeMod, Pro is active" in sh
    assert "Original WeMod files are back" in sh
    assert "See patch state above" in sh
    assert "See diagnosis above" in sh
    # Pro-active appears exactly once: inside the patch branch.
    assert sh.count("Pro is active") == 1


def test_sh_actions_match_patcher():
    """install.sh actions stay in sync with the patcher CLI."""
    text = SH.read_text()
    for action in ("patch", "restore", "status", "doctor"):
        assert action in text
    assert 'die "unknown action' in text


def test_no_hardcoded_user_paths():
    """No C:\\Users\\... / /home/<name> leaks; env vars only."""
    for path, text in ((PS1, PS1.read_text()), (SH, SH.read_text())):
        assert not re.search(r"C:\\\\Users\\\\[^$]", text), path
        assert not re.search(r"/home/[a-z_][a-z0-9_-]*", text), path


def test_readme_one_liners_match_scripts():
    """Readme paste lines equal the headers (copy-paste cannot drift)."""
    readme = (REPO / "readme.md").read_text()
    assert f"irm {PS1_URL} | iex" in readme
    assert f"curl -fsSL {SH_URL} | bash" in readme
