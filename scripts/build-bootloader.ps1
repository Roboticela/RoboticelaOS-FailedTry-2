# RoboticelaOS bootloader build — same role as `make` on a UNIX tree.
#
#   .\scripts\build-bootloader.ps1
#   .\scripts\build-bootloader.ps1 all
#   .\scripts\build-bootloader.ps1 efi
#   .\scripts\build-bootloader.ps1 test
#   .\scripts\build-bootloader.ps1 clean
#
# Or from the repo root:  .\build.ps1 <target>

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet(
        "all", "assets", "host", "efi", "image", "esp", "bootloader", "kernel",
        "test", "check", "qemu", "qemu-test", "run",
        "clean", "distclean", "info", "help"
    )]
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
. (Join-Path $PSScriptRoot "roboboot-build-common.ps1")
$script:RoboRebuild = [bool]$Rebuild

function Show-BuildHelp {
    Write-Host @"
RoboticelaOS / RoboBoot build

Usage:
  .\build.ps1 [target]
  .\scripts\build-bootloader.ps1 [target]

Targets:
  all          assets, host preview, UEFI app, ESP disk  (default)
  assets       bake fonts, pictures, dummy volumes
  host         native roboboot.exe
  efi          UEFI BOOTX64.EFI
  kernel       small x86_64 kernel.elf
  image, esp   GPT + FAT ESP disk image
  bootloader   efi + image
  test, check  host unit tests
  qemu-test    headless UEFI selftest
  qemu         boot the EFI image in QEMU
  run          launch the host preview
  clean        remove bootloader/build
  distclean    clean + baked assets
  info         print toolchain
  help         this text

Flags:
  -Rebuild     ignore timestamps and rebuild
  -Menu        start picker (run / qemu)
  -Headless    QEMU without a window (qemu)
"@
}

try {
    switch ($Target) {
        "help" { Show-BuildHelp; exit 0 }
        "info" {
            Write-RoboConfig
            Show-RoboBuildInfo
            exit 0
        }
        "clean" { Invoke-RoboClean; exit 0 }
        "distclean" { Invoke-RoboDistclean; exit 0 }
    }

    Write-RoboConfig
    Show-RoboBuildInfo

    switch ($Target) {
        "assets" { Invoke-RoboBake }
        "host" { Invoke-RoboBuildHost }
        "efi" { Invoke-RoboBuildEfi }
        "kernel" { Invoke-RoboBuildKernel }
        "image" { Invoke-RoboBuildEfi; Invoke-RoboBuildImage }
        "esp" { Invoke-RoboBuildEfi; Invoke-RoboBuildImage }
        "bootloader" { Invoke-RoboBuildEfi; Invoke-RoboBuildImage }
        "all" {
            Invoke-RoboBuildHost
            Invoke-RoboBuildEfi
            Invoke-RoboBuildImage
        }
        "test" { Invoke-RoboTestHost }
        "check" { Invoke-RoboTestHost }
        "run" {
            if (-not $SkipBuild) { Invoke-RoboBuildHost }
            & (Join-Path $PSScriptRoot "run-bootloader.ps1") -SkipBuild -Menu:$Menu -HoldAlt:$HoldAlt -FullScreen:$FullScreen -Width $Width -Height $Height
            exit $LASTEXITCODE
        }
        "qemu" {
            if (-not $SkipBuild) {
                Invoke-RoboBuildEfi
                $flag = ""
                if ($Menu -or $HoldAlt) { $flag = "menu" }
                Invoke-RoboBuildImage -Flags $flag
            }
            & (Join-Path $PSScriptRoot "run-bootloader-qemu.ps1") -SkipBuild -Menu:$Menu -HoldAlt:$HoldAlt -Headless:$Headless -Disk $Disk
            exit $LASTEXITCODE
        }
        "qemu-test" {
            & (Join-Path $PSScriptRoot "test-bootloader-qemu.ps1") -SkipBuild:$SkipBuild -Disk $Disk
            exit $LASTEXITCODE
        }
    }

    Write-Host ""
    Write-Step "OK" "RoboBoot $Target"
    if (Test-Path $script:HostOut) { Write-Host "         $($script:HostOut)" }
    if (Test-Path $script:EfiOut)  { Write-Host "         $($script:EfiOut)" }
    if (Test-Path $script:EspImg)  { Write-Host "         $($script:EspImg)" }
    Write-Host ""
    exit 0
}
catch {
    Write-Host ""
    Write-Host "  ERROR   $_"
    Write-Host ""
    exit 1
}
