#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# vendor/reframework/fetch-latest.ps1 (RE Requiem)
# ============================================================================
# Fetches the latest REFramework nightly (RE9.zip) from upstream. Nightlies are
# published as GitHub prereleases, so AllowPrerelease=true. Exits non-zero on
# any failure so install.cmd can fall back to vendor/reframework/RE9.zip.
# ============================================================================

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# --- CONFIG BLOCK ---------------------------------------------------------
# REFramework-nightly has no semver; pick the newest release (prerelease or not).
$Owner           = 'praydog'
$Repo            = 'REFramework-nightly'
$VersionPrefix   = ''
$AssetPattern    = '^RE9\.zip$'
$AllowPrerelease = $true
$TimeoutSec      = 30
# --- END CONFIG BLOCK -----------------------------------------------------

$headers = @{ "User-Agent" = "CameraUnlock-HeadTracking" }
$outputDir = Split-Path -Parent $OutputPath
if ($outputDir -and -not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

try {
    $apiUrl = "https://api.github.com/repos/$Owner/$Repo/releases?per_page=50"
    $releases = Invoke-RestMethod -Uri $apiUrl -Headers $headers -TimeoutSec $TimeoutSec
    $release = $releases | Where-Object {
        ($VersionPrefix -eq '' -or $_.tag_name.StartsWith($VersionPrefix)) -and
        ($AllowPrerelease -or -not $_.prerelease)
    } | Select-Object -First 1
    if (-not $release) { throw "No upstream release matches criteria for $Owner/$Repo." }
    $asset = $release.assets | Where-Object { $_.name -match $AssetPattern } | Select-Object -First 1
    if (-not $asset) { throw "Release $($release.tag_name) has no asset matching '$AssetPattern'." }
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $OutputPath -UseBasicParsing -TimeoutSec $TimeoutSec -Headers $headers
    $sha = (Get-FileHash -Path $OutputPath -Algorithm SHA256).Hash.ToLower()
    Write-Host "fetch-latest: tag=$($release.tag_name) asset=$($asset.name) sha256=$($sha.Substring(0,12))..."
    exit 0
} catch {
    Write-Error "fetch-latest: $_"
    exit 1
}
