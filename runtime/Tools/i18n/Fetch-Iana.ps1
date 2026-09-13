# Download IANA Language Subtag Registry for RoboI18N (Goal 4.1).
# Reads Tools/i18n/manifest.json. Does NOT run during normal Build.ps1.

$ErrorActionPreference = "Stop"
$Here = $PSScriptRoot
$OutDir = Join-Path $Here "source"
$ManifestPath = Join-Path $Here "manifest.json"

$manifest = Get-Content -Raw -Path $ManifestPath | ConvertFrom-Json
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$url = $manifest.iana.url
$dest = Join-Path $OutDir $manifest.iana.local_name
Write-Host "GET $url"
Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing
$hash = (Get-FileHash -Path $dest -Algorithm SHA256).Hash.ToLower()
$want = $manifest.iana.sha256.ToLower()
if ($hash -ne $want) {
    Write-Host "WARN checksum differs from manifest (registry may have updated)."
    Write-Host "  expected $want"
    Write-Host "  actual   $hash"
    Write-Host "Update File-Date pin + sha256 in robo_i18n_version.py / manifest.json after review."
} else {
    Write-Host "OK sha256"
}
Write-Host "Next: python $Here\generate_iana.py"
