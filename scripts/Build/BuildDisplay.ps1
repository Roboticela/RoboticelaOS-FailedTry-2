[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$helper = Join-Path $PSScriptRoot "..\Utilities\Invoke-RoboRuntimeBuild.ps1"
& $helper -Module Display -Configuration $Configuration -Verify:$Verify -Rebuild:$Rebuild
exit $LASTEXITCODE
