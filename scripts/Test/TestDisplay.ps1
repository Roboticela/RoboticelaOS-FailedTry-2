[CmdletBinding()]
param(
    [ValidateSet("Host", "Qemu")][string]$Target = "Host",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$helper = Join-Path $PSScriptRoot "..\Utilities\Invoke-RoboPartTest.ps1"
& $helper -Name "Display" -Target $Target -Configuration $Configuration -Verify:$Verify -Rebuild:$Rebuild `
    -Module "Display" -Filter "display." `
    -PipelineQemu "test-qemu-display"
exit $LASTEXITCODE
