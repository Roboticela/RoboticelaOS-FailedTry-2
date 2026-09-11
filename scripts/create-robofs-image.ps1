# Create and inspect a RoboFS v1 disk image.
#   .\scripts\create-robofs-image.ps1
#   .\scripts\create-robofs-image.ps1 -Image build\robofs.img -Blocks 256

param(
    [string]$Image = (Join-Path (Split-Path -Parent $PSScriptRoot) "build\robofs.img"),
    [int]$Blocks = 256
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$py = Get-Command python -ErrorAction SilentlyContinue
if (-not $py) { $py = Get-Command python3 -ErrorAction SilentlyContinue }
if (-not $py) { throw "python not found" }
$tool = Join-Path $repo "tools\robofs_tool.py"
New-Item -ItemType Directory -Force -Path (Split-Path $Image) | Out-Null
& $py.Source $tool format $Image --blocks $Blocks
if ($LASTEXITCODE -ne 0) { throw "format failed" }
& $py.Source $tool inspect $Image
if ($LASTEXITCODE -ne 0) { throw "inspect failed" }
& $py.Source $tool check $Image
if ($LASTEXITCODE -ne 0) { throw "check failed" }
Write-Host "RoboFS image ready: $Image"
