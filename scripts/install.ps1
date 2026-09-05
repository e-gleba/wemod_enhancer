#Requires -Version 5.1
<#
.SYNOPSIS
  Install WeMod Enhancer on Windows with one line.
.DESCRIPTION
  Close WeMod first, then paste this into PowerShell:

    irm https://raw.githubusercontent.com/e-gleba/wemod_enhancer/main/scripts/install.ps1 | iex

  What happens:
    1. Latest release is downloaded into Downloads\wemod_enhancer.
    2. Your newest WeMod install is found automatically.
    3. The patcher runs (backup first, so re-runs never stack changes).

  Variants (save the file first, then run one of these):

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
  param(
    [string]$Url,
    [string]$Destination
  )

  $zipPath = Join-Path ([System.IO.Path]::GetTempPath()) 'wemod_enhancer.zip'
  Write-Host 'Downloading WeMod Enhancer...'
  Invoke-WebRequest -Uri $Url -OutFile $zipPath

  if (Test-Path $Destination) {
    Remove-Item $Destination -Recurse -Force
  }
  Expand-Archive $zipPath -DestinationPath $Destination -Force
  Remove-Item $zipPath -Force -ErrorAction SilentlyContinue
}

function Find-Patcher {
  param([string]$Root)

  $binPath = Join-Path $Root 'bin\wemod_enhancer.py'
  if (Test-Path $binPath) {
    return $binPath
  }
  $flatPath = Join-Path $Root 'wemod_enhancer.py'
  if (Test-Path $flatPath) {
    return $flatPath
  }
  return $null
}

# --- run -------------------------------------------------------------------

Write-Host "WeMod Enhancer ($Action)" -ForegroundColor Cyan

# Resolve the target first: explicit folder wins, newest install second,
# patcher auto-detect last. Restore works even when the folder moved,
# because the patcher scans the same locations on its own.
$installDir = Find-WeModInstall
if ($installDir) {
  Write-Host "WeMod: $($installDir.FullName)" -ForegroundColor Green
} elseif ($Action -eq 'patch') {
  Write-Host 'No WeMod install found.' -ForegroundColor Red
  Write-Host 'Fix: install WeMod from https://www.wemod.com/download, run it once, log in, then re-run this script.'
  exit 1
} else {
  Write-Host 'No WeMod folder found, letting the patcher auto-detect it...' -ForegroundColor Yellow
}

Install-Package -Url (Get-PackageUrl) -Destination $WorkDir
Set-Location $WorkDir

$python = Ensure-Python

$patcher = Find-Patcher -Root $WorkDir
if (-not $patcher) {
  Write-Host "Patcher not found inside $WorkDir (bad download?). Delete the folder and re-run." -ForegroundColor Red
  exit 1
}

if ($installDir) {
  & $python $patcher $Action --install-dir $installDir.FullName
} else {
  & $python $patcher $Action
}
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}

if ($Action -eq 'patch') {
  Write-Host 'Done. Launch WeMod, Pro is active.' -ForegroundColor Green
} else {
  Write-Host 'Done. Original WeMod files are back.' -ForegroundColor Green
}
