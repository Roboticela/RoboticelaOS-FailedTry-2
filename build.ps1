# RoboticelaOS top-level build.
#   .\build.ps1
#   .\build.ps1 test
#   .\build.ps1 efi
#   .\build.ps1 help

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Target = "all",
    [switch]$Menu,
    [switch]$HoldAlt,
    [switch]$FullScreen,
    [switch]$Headless,
    [switch]$Rebuild,
    [switch]$SkipBuild,
    [int]$Width = 1280,
    [int]$Height = 800,
    [string]$Disk = "ide"
)

$ErrorActionPreference = "Stop"
$builder = Join-Path $PSScriptRoot "scripts\build-bootloader.ps1"
$forward = @{
    Target     = $Target
    Menu       = $Menu
    HoldAlt    = $HoldAlt
    FullScreen = $FullScreen
    Headless   = $Headless
    Rebuild    = $Rebuild
    SkipBuild  = $SkipBuild
    Width      = $Width
    Height     = $Height
    Disk       = $Disk
}
& $builder @forward
exit $LASTEXITCODE
