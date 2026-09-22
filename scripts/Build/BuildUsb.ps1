[CmdletBinding()]
param([switch]$Rebuild)
$ErrorActionPreference = "Stop"
$helper = Join-Path $PSScriptRoot "..\Utilities\Invoke-RoboPipeline.ps1"
& $helper -Command "build-image" -Rebuild:$Rebuild
exit $LASTEXITCODE
