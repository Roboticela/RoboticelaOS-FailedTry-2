# RoboticelaOS top-level build — compatibility shim.
#
# The project now has exactly one pipeline: scripts/robo. This shim exists so
# existing habits and CI jobs keep working, and it maps each legacy target onto
# the real command. It contains no build logic of its own.
#
#   .\build.ps1                 -> scripts/robo build-all
#   .\build.ps1 kernel          -> scripts/robo build-kernel
#   .\build.ps1 qemu            -> scripts/robo run-qemu
#   .\build.ps1 test            -> scripts/robo test-all --skip-qemu
#
# Prefer the pipeline directly:
#   scripts\robo help

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
    [string]$Disk = ""
)

$ErrorActionPreference = "Stop"

$map = @{
    "all"         = @{ Command = "build-all" }
    "assets"      = @{ Command = "build-assets" }
    "host"        = @{ Command = "build-bootloader" }
    "efi"         = @{ Command = "build-bootloader" }
    "bootloader"  = @{ Command = "build-bootloader" }
    "kernel"      = @{ Command = "build-kernel" }
    "userland"    = @{ Command = "build-userland" }
    "runtime"     = @{ Command = "build-runtime" }
    "combine"     = @{ Command = "build-image" }
    "image"       = @{ Command = "build-image" }
    "esp"         = @{ Command = "build-image" }
    "test"        = @{ Command = "test-all"; Args = @("--skip-qemu") }
    "check"       = @{ Command = "check" }
    "qemu-test"   = @{ Command = "test-qemu-kernel" }
    "qemu"        = @{ Command = "run-qemu" }
    "run"         = @{ Command = "run-bootloader" }
    "clean"       = @{ Command = "clean" }
    "distclean"   = @{ Command = "clean"; Args = @("--baked") }
    "info"        = @{ Command = "version" }
    "help"        = @{ Command = "help" }
}

if (-not $map.ContainsKey($Target)) {
    Write-Host ""
    Write-Host "  build.ps1: unknown target '$Target'" -ForegroundColor Red
    Write-Host "  known targets: $(($map.Keys | Sort-Object) -join ', ')" -ForegroundColor Gray
    Write-Host "  for the full command set: scripts\robo help" -ForegroundColor Gray
    Write-Host ""
    exit 2
}

$spec = $map[$Target]
$args = @($spec.Command)
if ($spec.ContainsKey("Args")) { $args += $spec.Args }

if ($Menu) { $args += "--menu" }
if ($HoldAlt) { $args += "--hold-alt" }
if ($FullScreen) { $args += "--fullscreen" }
if ($Headless) { $args += "--headless" }
if ($Rebuild) { $args += "--rebuild" }
if ($SkipBuild) { $args += "--skip-build" }
if ($Width -ne 1280) { $args += @("--width", "$Width") }
if ($Height -ne 800) { $args += @("--height", "$Height") }
if ($Disk) { $env:ROBO_QEMU_DISK = $Disk }

$entry = Join-Path $PSScriptRoot "scripts\pipeline\robo.ps1"
if (-not (Test-Path -LiteralPath $entry)) {
    Write-Host "  build.ps1: pipeline entry point missing: $entry" -ForegroundColor Red
    exit 2
}

& $entry @args
exit $LASTEXITCODE
