[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$ui = Join-Path $PSScriptRoot "BuildDesktopUi.ps1"
& $ui -Rebuild:$Rebuild
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$helper = Join-Path $PSScriptRoot "..\Utilities\Invoke-RoboRuntimeBuild.ps1"
& $helper -Module Desktop -Configuration $Configuration -Verify:$Verify -Rebuild:$Rebuild
exit $LASTEXITCODE
