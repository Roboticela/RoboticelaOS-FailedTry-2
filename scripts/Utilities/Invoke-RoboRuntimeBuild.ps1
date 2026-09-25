# Compiles one runtime module on the development machine. Build is never a QEMU mode.
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet("Core", "Memory", "Containers", "Unicode", "I18n", "Font", "Typography", "Text", "Graphics", "Display", "Window", "Compositor", "Input", "Html", "Css", "Layout", "Paint", "Rasterizer", "Desktop", "RoboJS", "Runtime")]
    [string]$Module,
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild,
    [switch]$SkipGuest
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo "Runtime\Tools\Build.ps1"
$splat = @{ Configuration = $Configuration }
if ($Module -ne "Runtime") {
    $splat.Module = $Module
    $splat.SkipGuest = $true
}
if ($SkipGuest) { $splat.SkipGuest = $true }
if ($Verify) { $splat.Verify = $true }
if ($Rebuild) { $splat.Rebuild = $true }
& $build @splat
exit $LASTEXITCODE
