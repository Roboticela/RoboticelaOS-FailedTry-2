# Build and launch RoboBoot (RoboticelaOS bootloader).
# Press Alt+M during the splash to open the startup picker.
#   .\scripts\run-bootloader.ps1
#   .\scripts\run-bootloader.ps1 -Menu
#   .\scripts\run-bootloader.ps1 -FullScreen

param(
    [switch]$Menu,
    [switch]$HoldAlt,
    [switch]$FullScreen,
    [int]$Width = 1280,
    [int]$Height = 800,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "roboboot-build-common.ps1")

Push-Location $script:Boot
try {
    if (-not $SkipBuild) { Invoke-RoboBuildHost }
    if (-not (Test-Path $script:HostOut)) { throw "roboboot.exe missing - build first" }

    $launch = @("--root", $script:Boot, "--width", "$Width", "--height", "$Height")
    if ($Menu) { $launch += "--menu" }
    if ($HoldAlt) { $launch += "--hold-alt" }
    if ($FullScreen) { $launch += "--fullscreen" }

    Write-Host "RoboBoot"
    Write-Host "  Press Alt+M during the splash for the startup picker."
    Write-Host "  Arrow keys + Enter select a volume. F10 opens the console. Esc quits."
    & $script:HostOut @launch
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
