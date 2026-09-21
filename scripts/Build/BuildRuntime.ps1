[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo "Runtime\Tools\Build.ps1"
$splat = @{ Configuration = $Configuration }
if ($Verify) { $splat.Verify = $true }
if ($Rebuild) { $splat.Rebuild = $true }
& $build @splat
exit $LASTEXITCODE
