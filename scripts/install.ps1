#Requires -Version 5.1
<#
.SYNOPSIS
  One-liner installer for WeMod Enhancer (Windows).
.DESCRIPTION
  Downloads the latest release, extracts it, auto-detects the newest
  WeMod install and runs the patcher. Idempotent and fail-safe.
.EXAMPLE
  irm https://raw.githubusercontent.com/e-gleba/wemod_enhancer/main/scripts/install.ps1 | iex
#>
[CmdletBinding()]
param(
  [string]$Version = "latest",
  [string]$Action = "patch",
  [switch]$Gui
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$Repo = "e-gleba/wemod_enhancer"
$WorkDir = Join-Path $env:USERPROFILE "Downloads\wemod_enhancer"

function Get-LatestUrl {
  if ($Version -eq "latest") {
    return "https://github.com/$Repo/releases/latest/download/wemod_enhancer-windows-msvc-amd64.zip"
  }
  return "https://github.com/$Repo/releases/download/$Version/wemod_enhancer-windows-msvc-amd64.zip"
}

function Find-WeMod {
  $candidates = Get-ChildItem "$env:LOCALAPPDATA\WeMod\app-*" -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName "resources\app.asar") } |
    Sort-Object { try { [version]($_.Name -replace '^app-','') } catch { [version]"0.0.0" } } -Descending
  return $candidates | Select-Object -First 1
}

function Ensure-Python {
  try {
    $v = & python --version 2>&1
    Write-Host "found $v" -ForegroundColor Green
    return "python"
  } catch {
    Write-Host "installing Python 3.13 via winget..." -ForegroundColor Yellow
    winget install --silent Python.Python.3.13 --accept-source-agreements --accept-package-agreements
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
    return "python"
  }
}

Write-Host "== WeMod Enhancer ($Action) ==" -ForegroundColor Cyan

$zipUrl = Get-LatestUrl
$zipPath = Join-Path $env:TEMP "wemod_enhancer.zip"
Write-Host "download $zipUrl"
Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath
if (Test-Path $WorkDir) { Remove-Item $WorkDir -Recurse -Force }
Expand-Archive $zipPath -DestinationPath $WorkDir -Force
Set-Location $WorkDir

$py = Ensure-Python
$wemod = Find-WeMod
if (-not $wemod) {
  Write-Host "error: no WeMod install found under %LOCALAPPDATA%\WeMod\app-*." -ForegroundColor Red
  Write-Host "install WeMod from https://www.wemod.com/download, run it once, log in, then re-run this script."
  exit 1
}
Write-Host "wemod: $($wemod.FullName)" -ForegroundColor Green

if ($Gui) {
  $gui = Join-Path $WorkDir "bin\wemod_enhancer_gui.exe"
  if (Test-Path $gui) { Start-Process $gui; exit 0 }
  Write-Host "warning: GUI not in this package, using CLI." -ForegroundColor Yellow
}

$patcher = Join-Path $WorkDir "bin\wemod_enhancer.py"
if (-not (Test-Path $patcher)) { $patcher = Join-Path $WorkDir "wemod_enhancer.py" }

& $py $patcher $Action --install-dir $wemod.FullName
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "done. Launch WeMod - Pro is active." -ForegroundColor Green
