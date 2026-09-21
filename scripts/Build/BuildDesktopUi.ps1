# Compiles the Vite + React desktop into plain HTML, CSS and JS, then writes the C page the OS scene includes.
[CmdletBinding()]
param([switch]$Rebuild)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$desktop = Join-Path $repo "Desktop"
$generated = Join-Path $repo "Runtime\Source\RoboDesktop\Generated\DesktopPage.h"
if (-not (Test-Path -LiteralPath (Join-Path $desktop "package.json"))) {
    throw "desktop UI source missing: $desktop"
}

function Test-RoboNode {
    try {
        $null = Get-Command node -ErrorAction Stop
        $null = Get-Command npm -ErrorAction Stop
        return $true
    } catch {
        return $false
    }
}

if (-not (Test-RoboNode)) {
    if (Test-Path -LiteralPath $generated) {
        Write-Host "Desktop UI: node/npm not on PATH; keeping $generated"
        exit 0
    }
    throw "node and npm are required to compile Desktop/ the first time"
}

Push-Location $desktop
try {
    $modules = Join-Path $desktop "node_modules"
    if ($Rebuild -or -not (Test-Path -LiteralPath $modules)) {
        if (Test-Path -LiteralPath (Join-Path $desktop "package-lock.json")) {
            npm ci
        } else {
            npm install
        }
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    npm run build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
if (-not (Test-Path -LiteralPath $generated)) {
    throw "desktop UI compile did not write $generated"
}
Write-Host "Desktop UI: $generated"
exit 0
