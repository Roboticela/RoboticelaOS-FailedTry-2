# Phase 7 gate (Goal 7.15).
#   .\scripts\test-phase7.ps1
#   .\scripts\test-phase7.ps1 -SkipBuild
#   .\scripts\test-phase7.ps1 -SkipQemu
#   .\scripts\test-phase7.ps1 -Visible

param(
    [switch]$SkipBuild,
    [switch]$SkipQemu,
    [switch]$Visible
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "runtime\Tools\Build.ps1"
$HostSmoke = Join-Path $PSScriptRoot "test-host-display.ps1"
$QemuSmoke = Join-Path $PSScriptRoot "test-qemu-display.ps1"

if (-not (Test-Path $Build)) {
    throw "Missing Build.ps1: $Build"
}

Write-Host "=== Goal 7.15 Phase 7 gate ==="

if (-not $SkipBuild) {
    Write-Host "BUILD  Verify + window.p7."
    & $Build -Configuration Debug -Verify -Test -Filter "window.p7."
    if ($LASTEXITCODE -ne 0) { throw "window.p7. acceptance failed" }
} else {
    Write-Host "TEST   window.p7. (no rebuild)"
    & $Build -Configuration Debug -Test -Filter "window.p7."
    if ($LASTEXITCODE -ne 0) { throw "window.p7. acceptance failed" }
}

Write-Host "TEST   window. regression"
& $Build -Configuration Debug -Test -Filter "window."
if ($LASTEXITCODE -ne 0) { throw "window. regression failed" }

Write-Host "TEST   display. regression"
& $Build -Configuration Debug -Test -Filter "display."
if ($LASTEXITCODE -ne 0) { throw "display. regression failed" }

Write-Host "SMOKE  host display"
$hostArgs = @("-SkipBuild", "-SkipQemuCheck")
if ($Visible) { $hostArgs += "-Visible" }
& $HostSmoke @hostArgs
if ($LASTEXITCODE -ne 0) { throw "host display smoke failed" }

if (-not $SkipQemu) {
    Write-Host "SMOKE  QEMU display"
    & $QemuSmoke -SkipBuild
    if ($LASTEXITCODE -ne 0) { throw "QEMU display smoke failed" }
} else {
    Write-Host "SKIP   QEMU display (-SkipQemu)"
}

Write-Host "PHASE 7 GATE PASS"
Write-Host "  Acceptance: window.p7.*"
Write-Host "  Regression: window.* display.*"
Write-Host "  Host smoke: OK"
if (-not $SkipQemu) {
    Write-Host "  QEMU smoke: OK"
}
Write-Host "  No Chromium dependency"
exit 0
