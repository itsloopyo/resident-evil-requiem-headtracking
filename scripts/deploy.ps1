#!/usr/bin/env pwsh
# Deploy build to RE Requiem game directory.
# REFramework is sourced from the committed vendor/reframework/RE9.zip; bump
# it via `pixi run update-deps`. No network access at deploy time.

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

# Install REFramework from vendored copy if not already present.
$reframeworkDll = Join-Path $gamePath "dinput8.dll"
if (-not (Test-Path $reframeworkDll)) {
    $vendorZip = Join-Path $projectDir "vendor\reframework\RE9.zip"
    if (-not (Test-Path $vendorZip)) {
        throw "REFramework not installed and vendored copy missing at $vendorZip. Run 'pixi run update-deps' to fetch the latest, then retry."
    }
    Write-Host "REFramework not found. Extracting bundled copy..." -ForegroundColor Yellow
    Expand-Archive -Path $vendorZip -DestinationPath $gamePath -Force
    if (-not (Test-Path $reframeworkDll)) {
        throw "REFramework install failed: dinput8.dll not found after extraction."
    }
    Write-Host "  REFramework installed from vendor/reframework/RE9.zip." -ForegroundColor Green
} else {
    Write-Host "REFramework present, skipping loader install." -ForegroundColor Gray
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
