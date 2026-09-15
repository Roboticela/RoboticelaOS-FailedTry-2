# RoboticelaOS pipeline launcher for a PowerShell prompt.
#
#   .\scripts\robo.ps1 check
#   .\scripts\robo.ps1 build-all
#   .\scripts\robo.ps1 test-all
#   .\scripts\robo.ps1 run-qemu
#
# Identical to scripts\robo.cmd and ./scripts/robo; all three forward to
# scripts/pipeline/robo.ps1 and return its exit code.

$ErrorActionPreference = "Stop"
$entry = Join-Path $PSScriptRoot "pipeline\robo.ps1"
if (-not (Test-Path -LiteralPath $entry)) {
    Write-Error "robo: pipeline entry point not found: $entry"
    exit 2
}

& $entry @args
exit $LASTEXITCODE
