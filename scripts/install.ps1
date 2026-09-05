#Requires -Version 5.1
<#
.SYNOPSIS
  Install WeMod Enhancer on Windows with one line.
.DESCRIPTION
  Close WeMod first, then paste this into PowerShell:

    irm https://raw.githubusercontent.com/e-gleba/wemod_enhancer/main/scripts/install.ps1 | iex

  The script downloads the latest release into Downloads\wemod_enhancer,
  finds your newest WeMod install, and runs the patcher. Safe to re-run:
  patching starts from the automatic backup, so it never stacks changes.

  Variants (download the file first, then run one of these):

    .\install.ps1 -Action patch     # default: unlock Pro
    .\install.ps1 -Action restore   # undo: put the original files back
    .\install.ps1 -Version v1.0.7   # pin a release instead of latest
#>
[CmdletBinding()]
param(
  [ValidateSet('patch', 'restore')]
  [string]$Action = 'patch',

  [string]$Version = 'latest'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$PackageName = 'wemod_enhancer-windows-msvc-amd64.zip'
$ReleaseBase = 'https://github.com/e-gleba/wemod_enhancer/releases'
$WorkDir = Join-Path $env:USERPROFILE 'Downloads\wemod_enhancer'

function Get-PackageUrl {
  if ($Version -eq 'latest') {
    return "$ReleaseBase/latest/download/$PackageName"
  }
  return "$ReleaseBase/download/$Version/$PackageName"
}

function Find-WeModInstall {
  # Newest app-* folder holding resources\app.asar. Numeric sort, so
  # app-10 does not lose to app-9 the way plain name sort would.
  $apps = Get-ChildItem "$env:LOCALAPPDATA\WeMod\app-*" -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'resources\app.asar') } |
    Sort-Object { try { [version]($_.Name -replace '^app-', '') } catch { [version]'0.0.0' } } -Descending
  return $apps | Select-Object -First 1
}

function Ensure-Python {
  # Returns a working `python` command. Installs 3.13 via winget once,
  # then refreshes PATH in this session so the next line just works.
  try {
    $found = & python --version 2>&1
    Write-Host "Found $found." -ForegroundColor Green
    return 'python'
  } catch {
    Write-Host 'No Python found, installing 3.13 via winget (one-time setup)...' -ForegroundColor Yellow
    winget install --silent Python.Python.3.13 --accept-source-agreements --accept-package-agreements
    $machine = [System.Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [System.Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
    return 'python'
  }
}

function Install-Package {
  param([string]$Url, [string]$Destination)

  $zipPath = Join-Path ([System.IO.Path]::GetTempPath()) 'wemod_enhancer.zip'
  Write-Host 'Downloading WeMod Enhancer...'
  Invoke-WebRequest -Uri $Url -OutFile $zipPath

  if (Test-Path $Destination) {
    Remove-Item $Destination -Recurse -Force
  }
  Expand-Archive $zipPath -DestinationPath $Destination -Force
  Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
}

# --- run -------------------------------------------------------------------

Write-Host "WeMod Enhancer ($Action)" -ForegroundColor Cyan

if ($Action -eq 'patch') {
  $weMod = Find-WeModInstall
  if (-not $weMod) {
    Write-Host 'No WeMod install found.' -ForegroundColor Red
    Write-Host 'Fix: install WeMod from https://www.wemod.com/download, run it once, log in, then re-run this script.'
    exit 1
  }
  Write-Host "WeMod: $($weMod.FullName)" -ForegroundColor Green
}

Install-Package -Url (Get-PackageUrl) -Destination $WorkDir
Set-Location $WorkDir

$python = Ensure-Python

$patcher = Join-Path $WorkDir 'bin\wemod_enhancer.py'
if (-not (Test-Path $patcher)) {
  $patcher = Join-Path $WorkDir 'wemod_enhancer.py'
}
if (-not (Test-Path $patcher)) {
  Write-Host "Patcher not found inside $WorkDir (bad download?). Delete the folder and re-run." -ForegroundColor Red
  exit 1
}

if ($Action -eq 'patch') {
  & $python $patcher patch --install-dir $weMod.FullName
} else {
  # Restore auto-detects too: pass the folder only when patch did.
  & $python $patcher restore --install-dir $weMod.FullName
}
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

Write-Host 'Done. Launch WeMod, Pro is active.' -ForegroundColor Green
