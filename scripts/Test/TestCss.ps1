[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo "Runtime\Tools\Build.ps1"
$splat = @{
    Configuration = $Configuration
    Module = "Css"
    SkipGuest = $true
    BuildTests = $true
}
if ($Verify) { $splat.Verify = $true }
if ($Rebuild) { $splat.Rebuild = $true }
& $build @splat
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$exe = Join-Path $repo "Build\Runtime\RoboTest.host.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "host test binary missing: $exe" }
& $exe "--filter=css."
exit $LASTEXITCODE
