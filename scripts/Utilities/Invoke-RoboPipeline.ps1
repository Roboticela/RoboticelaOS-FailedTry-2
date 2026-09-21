# One pipeline command, always compiled on the host.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Command,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$robo = Join-Path $repo "scripts\pipeline\robo.ps1"
if (-not (Test-Path -LiteralPath $robo)) {
    throw "pipeline missing: $robo"
}
$pipelineArgs = @($Command)
if ($Rebuild) { $pipelineArgs += "--rebuild" }
& $robo @pipelineArgs
exit $LASTEXITCODE
