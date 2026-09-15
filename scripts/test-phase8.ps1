# Phase 8.1 gate (Goal 8.1).
#   .\scripts\test-phase8.ps1
#   .\scripts\test-phase8.ps1 -SkipBuild

param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "runtime\Tools\Build.ps1"

if (-not (Test-Path $Build)) {
    throw "Missing Build.ps1: $Build"
}

Write-Host "=== Goal 8.1 Phase 8 gate ==="

if (-not $SkipBuild) {
    Write-Host "BUILD  Verify + html.p8."
    & $Build -Configuration Debug -Verify -Test -Filter "html.p8."
    if ($LASTEXITCODE -ne 0) { throw "html.p8. acceptance failed" }
} else {
    Write-Host "TEST   html.p8. (no rebuild)"
    & $Build -Configuration Debug -Test -Filter "html.p8."
    if ($LASTEXITCODE -ne 0) { throw "html.p8. acceptance failed" }
}

Write-Host "TEST   html. regression"
& $Build -Configuration Debug -Test -Filter "html."
if ($LASTEXITCODE -ne 0) { throw "html. regression failed" }

Write-Host "PHASE 8.1 GATE PASS"
Write-Host "  Acceptance: html.p8.*"
Write-Host "  Regression: html.*"
Write-Host "  No Chromium dependency"
exit 0
