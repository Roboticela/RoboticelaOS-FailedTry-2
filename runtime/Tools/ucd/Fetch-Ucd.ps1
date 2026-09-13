# Download official Unicode / Emoji data for RoboUnicode table generation.
# Reads Tools/ucd/manifest.json (authoritative URLs + expected checksums).
# Does NOT regenerate runtime tables — run: python generate_all.py

param(
    [switch]$SkipChecksumVerify
)

$ErrorActionPreference = "Stop"
$Here = $PSScriptRoot
$OutDir = Join-Path $Here "source"
$ManifestPath = Join-Path $Here "manifest.json"

if (-not (Test-Path $ManifestPath)) {
    throw "Missing manifest: $ManifestPath"
}

$manifest = Get-Content -Raw -Path $ManifestPath | ConvertFrom-Json
$Version = $manifest.unicode_version
$EmojiVersion = $manifest.emoji_version
Write-Host "Unicode $Version / Emoji $EmojiVersion (from manifest.json)"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

foreach ($f in $manifest.files) {
    $dest = Join-Path $OutDir $f.name
    Write-Host "GET $($f.url)"
    Invoke-WebRequest -Uri $f.url -OutFile $dest -UseBasicParsing
    $len = (Get-Item $dest).Length
    Write-Host ("OK {0} ({1} bytes)" -f $f.name, $len)

    if (-not $SkipChecksumVerify -and $f.sha256) {
        $hash = (Get-FileHash -Path $dest -Algorithm SHA256).Hash.ToLower()
        $want = $f.sha256.ToLower()
        if ($hash -ne $want) {
            throw "Checksum mismatch for $($f.name)`n  expected $want`n  actual   $hash"
        }
        Write-Host "OK sha256 $($f.name)"
    }
}

Write-Host ""
Write-Host "Done. Next (explicit regeneration — not part of normal builds):"
Write-Host "  python $Here\generate_all.py"
Write-Host "  python $Here\generate_all.py --repro"
Write-Host "  python $Here\verify_pipeline.py --sizes"
