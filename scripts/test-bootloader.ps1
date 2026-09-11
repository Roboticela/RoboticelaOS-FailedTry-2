# Build and run RoboBoot unit + session tests.
#   .\scripts\test-bootloader.ps1

param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "roboboot-build-common.ps1")

Push-Location $script:Boot
try {
    if (-not $SkipBuild) { Invoke-RoboBuildTest }
    if (-not (Test-Path $script:TestOut)) { throw "roboboot-test.exe missing - build first" }
    & $script:TestOut $script:Boot
    $bootStatus = $LASTEXITCODE
    try {
        Invoke-RoboTestPmm
    } catch {
        if ($bootStatus -eq 0) { throw }
        Write-Host "PMM host tests failed"
        exit 1
    }
    if ($bootStatus -ne 0) { exit $bootStatus }
    exit 0
}
finally {
    Pop-Location
}
