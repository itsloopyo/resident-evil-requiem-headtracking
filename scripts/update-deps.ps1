#!/usr/bin/env pwsh
#Requires -Version 5.1
# Refresh the vendored REFramework nightly to the latest upstream that ships
# RE9.zip. Vendored zip is the install-time source of truth, so this is a
# manual bump-and-commit step. CI does not call this.
# See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'pixi run sync' to update the cameraunlock-core submodule."
}
Import-Module $module -Force

# Praydog repackaged the nightlies on 2026-04-25: per-game zips (RE9.zip,
# RE2.zip, ...) were collapsed into a single REFramework.zip that ships
# dinput8.dll + reframework_revision.txt and works across all supported
# games. VR-specific runtimes moved to a separate VR.zip.
$out = Join-Path $projectDir 'vendor/reframework'
Update-VendoredLoader `
    -Name 'reframework' `
    -OutputDir $out `
    -OutputFileName 'REFramework.zip' `
    -Owner 'praydog' -Repo 'REFramework-nightly' `
    -AssetPattern '^REFramework\.zip$' `
    -AllowPrerelease `
    -LicenseUrl 'https://raw.githubusercontent.com/praydog/REFramework/master/LICENSE' | Out-Null

Write-Host ""
Write-Host "vendor/reframework refreshed. Review and commit." -ForegroundColor Green
