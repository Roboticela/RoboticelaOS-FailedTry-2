[CmdletBinding()]
param([switch]$Rebuild)
$ErrorActionPreference = "Stop"
$helper = Join-Path $PSScriptRoot "..\Utilities\Invoke-RoboPipeline.ps1"
& $helper -Command "build-bootloader" -Rebuild:$Rebuild
exit $LASTEXITCODE
