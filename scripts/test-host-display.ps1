# Host display smoke (Goal 7.13).
#   .\scripts\test-host-display.ps1
#   .\scripts\test-host-display.ps1 -SkipBuild
#   .\scripts\test-host-display.ps1 -Visible

param(
    [switch]$SkipBuild,
    [switch]$Visible,
    [switch]$SkipQemuCheck
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "runtime\Tools\Build.ps1"

if (-not (Test-Path $Build)) {
    throw "Missing Build.ps1: $Build"
}

Write-Host "=== Goal 7.13 host/native display smoke ==="

if (-not $SkipBuild) {
    Write-Host "BUILD  runtime (Verify + host.* tests)"
    & $Build -Configuration Debug -Verify -Test -Filter "host."
    if ($LASTEXITCODE -ne 0) { throw "Host display filtered build/test failed" }
} else {
    Write-Host "TEST   host. (no rebuild)"
    & $Build -Configuration Debug -Test -Filter "host."
    if ($LASTEXITCODE -ne 0) { throw "Host display filtered test failed" }
}

if ($Visible) {
    Write-Host "VISIBLE smoke (ROBO_WINDOW_SMOKE=1)"
    $env:ROBO_WINDOW_SMOKE = "1"
    try {
        & $Build -Configuration Debug -Test -Filter "window.host.smoke"
        if ($LASTEXITCODE -ne 0) { throw "Visible host smoke failed" }
    } finally {
        Remove-Item Env:ROBO_WINDOW_SMOKE -ErrorAction SilentlyContinue
    }
}

if (-not $SkipQemuCheck) {
    Write-Host "CHECK  QEMU sim suite still green"
    & $Build -Configuration Debug -Test -Filter "window.qemu."
    if ($LASTEXITCODE -ne 0) { throw "QEMU display suite failed" }
}

Write-Host "HOST DISPLAY SMOKE PASS"
Write-Host "  Backend: Win32 GDI CreateDIBSection + BitBlt"
Write-Host "  Display: EnumDisplayMonitors"
Write-Host "  No Chromium dependency"
exit 0
