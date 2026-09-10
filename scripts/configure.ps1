# Probe the toolchain and write bootloader/build/config.
# Same idea as ./configure before make.
#
#   .\scripts\configure.ps1
#   .\build.ps1

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "roboboot-build-common.ps1")

try {
    Write-RoboConfig
    Show-RoboBuildInfo
    Write-Host "  wrote   $($script:ConfigFile)"
    Write-Host ""
    Write-Host "  Next:   .\build.ps1"
    Write-Host ""
    exit 0
}
catch {
    Write-Host ""
    Write-Host "  ERROR   $_"
    Write-Host ""
    exit 1
}
