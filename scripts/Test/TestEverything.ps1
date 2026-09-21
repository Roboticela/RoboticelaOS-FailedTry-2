[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild,
    [switch]$SkipQemu
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$robo = Join-Path $repo "scripts\pipeline\robo.ps1"
$pipelineArgs = @("test-all")
if ($SkipQemu) { $pipelineArgs += "--skip-qemu" }
if ($Rebuild) { $pipelineArgs += "--rebuild" }
& $robo @pipelineArgs
exit $LASTEXITCODE
