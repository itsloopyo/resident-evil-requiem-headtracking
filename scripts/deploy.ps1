#!/usr/bin/env pwsh
# Deploy build to RE Requiem game directory

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$configuration = if ($args[0]) { $args[0] } else { "Debug" }

Write-Host "Deploying $configuration build to Resident Evil Requiem..." -ForegroundColor Cyan

# Detect game path
$gamePath = & "$scriptDir\detect-game.ps1"
if ($LASTEXITCODE -ne 0) {
    exit 1
}

Write-Host "Game directory: $gamePath" -ForegroundColor Gray

# Check for REFramework — auto-install or update to latest nightly
$reframeworkDll = Join-Path $gamePath "dinput8.dll"
$refVersionFile = Join-Path $gamePath "reframework\.nightly-tag"

# Resolve latest nightly release tag via GitHub API
Write-Host "  Checking latest REFramework nightly..." -ForegroundColor Gray
$releaseInfo = $null
try {
    $releaseJson = curl.exe -sL "https://api.github.com/repos/praydog/REFramework-nightly/releases/latest"
    $releaseInfo = $releaseJson | ConvertFrom-Json
} catch {
    Write-Host "  Could not check for updates (network error). Skipping." -ForegroundColor Yellow
}

$needsInstall = -not (Test-Path $reframeworkDll)
$needsUpdate = $false
if ($releaseInfo -and (Test-Path $reframeworkDll)) {
    $latestTag = $releaseInfo.tag_name
    $installedTag = if (Test-Path $refVersionFile) { (Get-Content $refVersionFile -Raw).Trim() } else { "" }
    if ($latestTag -ne $installedTag) {
        Write-Host "  REFramework update available: $installedTag -> $latestTag" -ForegroundColor Yellow
        $needsUpdate = $true
    } else {
        Write-Host "REFramework up to date ($latestTag)." -ForegroundColor Gray
    }
}

if ($needsInstall -or $needsUpdate) {
    if ($needsInstall) {
        Write-Host "REFramework not found. Installing..." -ForegroundColor Yellow
    }

    if (-not $releaseInfo) {
        throw "Cannot install REFramework: failed to fetch release info. Install manually from: https://www.nexusmods.com/residentevilrequiem/mods"
    }

    $refUrl = $releaseInfo.assets | Where-Object { $_.name -eq "RE9.zip" } | Select-Object -ExpandProperty browser_download_url
    if (-not $refUrl) {
        throw "Could not find RE9.zip in latest nightly release. Install manually from: https://www.nexusmods.com/residentevilrequiem/mods"
    }

    $refZip = Join-Path $env:TEMP "REFramework_RE9_install.zip"
    Write-Host "  Downloading REFramework nightly..." -ForegroundColor Gray
    curl.exe -fL -o $refZip $refUrl
    if ($LASTEXITCODE -ne 0) {
        throw "REFramework download failed. Install manually from: https://www.nexusmods.com/residentevilrequiem/mods"
    }

    Write-Host "  Extracting to game directory..." -ForegroundColor Gray
    Expand-Archive -Path $refZip -DestinationPath $gamePath -Force
    Remove-Item $refZip -ErrorAction SilentlyContinue

    if (-not (Test-Path $reframeworkDll)) {
        throw "REFramework install failed: dinput8.dll not found after extraction."
    }

    # Record installed version
    $refDir = Join-Path $gamePath "reframework"
    if (-not (Test-Path $refDir)) { New-Item -ItemType Directory -Path $refDir -Force | Out-Null }
    $releaseInfo.tag_name | Out-File -FilePath $refVersionFile -Encoding utf8 -NoNewline

    $action = if ($needsInstall) { "installed" } else { "updated" }
    Write-Host "  REFramework $action ($($releaseInfo.tag_name))!" -ForegroundColor Green
} elseif (-not $releaseInfo -and (Test-Path $reframeworkDll)) {
    Write-Host "REFramework found (offline, skipped update check)." -ForegroundColor Gray
}

# Create plugins directory
$pluginsDir = Join-Path $gamePath "reframework\plugins"
if (-not (Test-Path $pluginsDir)) {
    New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
    Write-Host "  Created: reframework/plugins/" -ForegroundColor Green
}

# Source files
$sourceDll = Join-Path $projectDir "bin\$configuration\RE9HeadTracking.dll"
$sourceIni = Join-Path $projectDir "HeadTracking.ini"

if (-not (Test-Path $sourceDll)) {
    Write-Error "Build artifact not found: $sourceDll"
    Write-Host "Run 'pixi run build' first." -ForegroundColor Yellow
    exit 1
}

# Target files
$targetDll = Join-Path $pluginsDir "RE9HeadTracking.dll"
$targetIni = Join-Path $pluginsDir "HeadTracking.ini"

# Backup existing
if (Test-Path $targetDll) {
    $backupDll = "$targetDll.bak"
    Copy-Item $targetDll $backupDll -Force
    Write-Host "  Backed up existing DLL" -ForegroundColor Gray
}

# Copy DLL
Copy-Item $sourceDll $targetDll -Force
Write-Host "  Copied: RE9HeadTracking.dll" -ForegroundColor Green

# Copy INI only if it doesn't exist (preserve user settings)
if (-not (Test-Path $targetIni)) {
    if (Test-Path $sourceIni) {
        Copy-Item $sourceIni $targetIni -Force
        Write-Host "  Copied: HeadTracking.ini (default config)" -ForegroundColor Green
    }
} else {
    Write-Host "  Skipped: HeadTracking.ini (preserving existing config)" -ForegroundColor Gray
}

Write-Host ""
Write-Host "Deployment complete!" -ForegroundColor Green
Write-Host "Files deployed to: $pluginsDir" -ForegroundColor Cyan
