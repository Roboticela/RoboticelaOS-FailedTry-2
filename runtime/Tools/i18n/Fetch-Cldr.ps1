# Download selected CLDR 46 JSON files for RoboI18N (Goal 4.2).
# Reads Tools/i18n/manifest.json. Does NOT run during normal Build.ps1.

$ErrorActionPreference = "Stop"
$Here = $PSScriptRoot
$ManifestPath = Join-Path $Here "manifest.json"
$manifest = Get-Content -Raw -Path $ManifestPath | ConvertFrom-Json

$cldr = $manifest.cldr
$version = $cldr.package
$OutRoot = Join-Path $Here "source" ("cldr-" + $version)
$SuppDir = Join-Path $OutRoot "supplemental"
$NumDir = Join-Path $OutRoot "numbers"
New-Item -ItemType Directory -Force -Path $SuppDir | Out-Null
New-Item -ItemType Directory -Force -Path $NumDir | Out-Null

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLower()
}

function Fetch-File([string]$Url, [string]$Dest, [string]$ExpectedSha) {
    Write-Host "GET $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing
    if ($ExpectedSha -and $ExpectedSha.Length -gt 0) {
        $hash = Get-Sha256 $Dest
        if ($hash -ne $ExpectedSha.ToLower()) {
            throw "checksum mismatch for $Dest`n  expected $ExpectedSha`n  actual   $hash"
        }
        Write-Host "OK sha256 $([IO.Path]::GetFileName($Dest))"
    }
}

foreach ($f in $cldr.supplemental_files) {
    $dest = Join-Path $SuppDir $f.name
    Fetch-File $f.url $dest $f.sha256
}

# Core + international coverage locales for numbers/*.json
$localeList = @(
    "und", "root", "en", "en-GB", "ar", "ar-SA", "ar-EG", "fa", "fa-AF",
    "ur", "hi", "bn", "th", "zh", "zh-Hans", "zh-Hant", "ja", "ko", "he",
    "de", "fr", "es", "sr", "sr-Latn", "sr-Cyrl"
)

$baseUrl = $cldr.numbers_base_url.TrimEnd("/")
foreach ($loc in $localeList) {
    $url = "$baseUrl/$loc.json"
    $dest = Join-Path $NumDir "$loc.json"
    try {
        Fetch-File $url $dest ""
    } catch {
        Write-Host "WARN skip $loc : $_"
    }
}

Write-Host "Fetched supplemental into $SuppDir"
Write-Host "Numbers samples in $NumDir (full set may already exist from prior fetch)."
Write-Host "Next: python $Here\generate_cldr.py --write-golden"
