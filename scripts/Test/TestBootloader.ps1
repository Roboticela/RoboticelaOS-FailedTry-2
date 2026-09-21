[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

$robo = Join-Path $repo "scripts\pipeline\robo.ps1"
if (-not (Test-Path -LiteralPath $robo)) {
    throw "pipeline missing: $robo"
}
$pipelineArgs = @("test-bootloader")
if ($Rebuild) { $pipelineArgs += "--rebuild" }
& $robo @pipelineArgs
exit $LASTEXITCODE
