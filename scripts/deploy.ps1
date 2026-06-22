#!/usr/bin/env pwsh
#Requires -Version 5.1
# Thin wrapper - dev-deploy orchestration lives in
# cameraunlock-core/powershell/DevDeploy.psm1.

param(
    [Parameter(Mandatory=$true, Position=0)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration,
    [Parameter(Mandatory=$false, Position=1)]
    [string]$GivenPath,
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$RemainingArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectRoot "cameraunlock-core\powershell\DevDeploy.psm1") -Force
Import-Module (Join-Path $projectRoot "cameraunlock-core\powershell\ModDeployment.psm1") -Force
$buildOutput = Join-Path $projectRoot "bin\$Configuration"
$configFile = Join-Path $projectRoot 'HeadTracking.ini'
$vendorRefZip = Join-Path $projectRoot 'vendor\reframework\REFramework.zip'
$result = Invoke-DevDeployREFramework `
    -GameId 'resident-evil-requiem' `
    -GameDisplayName 'Resident Evil Requiem' `
    -BuildOutputPath $buildOutput `
    -ModDllName 'RE9HeadTracking.dll' `
    -ConfigFile $configFile `
    -VendorReframeworkZip $vendorRefZip `
    -ExtraDlls @() `
    -GivenPath $GivenPath

Write-DeploymentSuccess `
    -ModName "Head Tracking mod" `
    -DeployPath $result.DeployedDllPath `
    -Controls @(
        "Home      - Recenter head tracking",
        "End       - Toggle head tracking on/off",
        "Page Up   - Cycle tracking mode (full / rotation-only / position-only)",
        "Page Down - Toggle yaw mode (world / local)",
        "F9        - Toggle world-anchored GUI marker hiding (diagnostic)",
        "",
        "No nav cluster? Chords: Ctrl+Shift+ T=Recenter Y=Toggle G=Mode H=Yaw"
    )