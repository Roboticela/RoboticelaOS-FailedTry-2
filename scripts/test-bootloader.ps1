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
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
