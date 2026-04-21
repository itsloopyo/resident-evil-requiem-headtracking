#!/usr/bin/env pwsh
#Requires -Version 5.1
# Refresh vendored REFramework nightly from upstream so every build picks up
# the freshest known-good fallback. Called as a dependency of `pixi run build`.
# Failures are non-fatal - the existing vendored copy is kept.
# See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$moduleCandidates = @(
    (Join-Path $projectDir "cameraunlock-core/powershell/ModLoaderSetup.psm1"),
    (Join-Path $projectDir "../cameraunlock-core/powershell/ModLoaderSetup.psm1")
)
$modulePath = $null
foreach ($candidate in $moduleCandidates) {
    if (Test-Path $candidate) {
        $content = Get-Content $candidate -Raw
        if ($content -match 'Refresh-VendoredLoader') {
            $modulePath = $candidate
            break
        }
    }
}
if (-not $modulePath) {
    Write-Warning "ModLoaderSetup.psm1 with Refresh-VendoredLoader not found. Run 'pixi run sync' to bump submodule. Keeping existing vendored copy."
    return
}
Import-Module $modulePath -Force

$vendorRfDir = Join-Path $projectDir "vendor/reframework"
Write-Host "Refreshing vendor/reframework from upstream..." -ForegroundColor Cyan
try {
    Refresh-VendoredLoader `
        -Name 'reframework' `
        -OutputDir $vendorRfDir `
        -OutputFileName 'RE9.zip' `
        -Owner 'praydog' -Repo 'REFramework-nightly' `
        -AssetPattern '^RE9\.zip$' `
        -AllowPrerelease `
        -LicenseUrl 'https://raw.githubusercontent.com/praydog/REFramework/master/LICENSE' | Out-Null
} catch {
    Write-Warning "vendor/reframework refresh failed ($_). Existing vendored copy will be used."
}

$vendorRfZip = Join-Path $vendorRfDir "RE9.zip"
if (-not (Test-Path $vendorRfZip)) {
    throw "Bundled REFramework fallback missing after refresh: $vendorRfZip"
}
