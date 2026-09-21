[CmdletBinding()]
param(
    [ValidateSet("Host", "Qemu")][string]$Target = "Host",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$helper = Join-Path $PSScriptRoot "..\Utilities\Invoke-RoboPartTest.ps1"
& $helper -Name "I18n" -Target $Target -Configuration $Configuration -Verify:$Verify -Rebuild:$Rebuild `
    -Module "I18n" -Filter "i18n."
exit $LASTEXITCODE
