[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$script = Join-Path $PSScriptRoot "TestDesktop.ps1"
& $script -Target Qemu -Configuration $Configuration -Verify:$Verify -Rebuild:$Rebuild
exit $LASTEXITCODE
