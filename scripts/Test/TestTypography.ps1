[CmdletBinding()]
param(
    [ValidateSet("Host", "Qemu")][string]$Target = "Host",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$helper = Join-Path $PSScriptRoot "..\Utilities\Invoke-RoboPartTest.ps1"
# The typography binding is a host-layer concern: it resolves roles to faces and produces the numbers
# a layout and a paint pass consume. There is no QEMU-only behaviour in this module, and the phase's
# guest-visible check is the desktop's own (`TestDesktop.ps1 -Target Qemu`).
& $helper -Name "Typography" -Target $Target -Configuration $Configuration -Verify:$Verify -Rebuild:$Rebuild `
    -Module "Typography" -Filter "typography."
exit $LASTEXITCODE
