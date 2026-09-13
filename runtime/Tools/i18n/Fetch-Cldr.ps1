# Download selected CLDR 46 JSON files for RoboI18N (Goals 4.2–4.10).
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
$DatesDir = Join-Path $OutRoot "dates"
New-Item -ItemType Directory -Force -Path $SuppDir | Out-Null
New-Item -ItemType Directory -Force -Path $NumDir | Out-Null
New-Item -ItemType Directory -Force -Path $DatesDir | Out-Null

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

# Gregorian calendar names/patterns + dateFields for Goals 4.5–4.7
$dateLocales = @(
    "en", "en-GB", "ur", "ar", "fa", "he", "hi", "bn", "th", "zh", "zh-Hans",
    "ja", "ko", "de", "fr", "es", "ru", "pl"
)
$datesBase = $cldr.dates_base_url.TrimEnd("/")
foreach ($loc in $dateLocales) {
    $dir = Join-Path $DatesDir $loc
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $url = "$datesBase/$loc/ca-gregorian.json"
    $dest = Join-Path $dir "ca-gregorian.json"
    try {
        Fetch-File $url $dest ""
    } catch {
        Write-Host "WARN skip dates $loc/ca-gregorian : $_"
    }
    $urlDf = "$datesBase/$loc/dateFields.json"
    $destDf = Join-Path $dir "dateFields.json"
    try {
        Fetch-File $urlDf $destDf ""
    } catch {
        Write-Host "WARN skip dates $loc/dateFields : $_"
    }
}

# Non-Gregorian calendars live in separate cldr-cal-*-full packages.
$calPackages = @(
    @{ pkg = "cldr-cal-buddhist-full"; loc = "th"; file = "ca-buddhist.json" },
    @{ pkg = "cldr-cal-buddhist-full"; loc = "en"; file = "ca-buddhist.json" },
    @{ pkg = "cldr-cal-japanese-full"; loc = "ja"; file = "ca-japanese.json" },
    @{ pkg = "cldr-cal-japanese-full"; loc = "en"; file = "ca-japanese.json" },
    @{ pkg = "cldr-cal-islamic-full"; loc = "ar"; file = "ca-islamic.json" },
    @{ pkg = "cldr-cal-islamic-full"; loc = "en"; file = "ca-islamic.json" },
    @{ pkg = "cldr-cal-hebrew-full"; loc = "he"; file = "ca-hebrew.json" },
    @{ pkg = "cldr-cal-hebrew-full"; loc = "en"; file = "ca-hebrew.json" },
    @{ pkg = "cldr-cal-persian-full"; loc = "fa"; file = "ca-persian.json" },
    @{ pkg = "cldr-cal-persian-full"; loc = "en"; file = "ca-persian.json" },
    @{ pkg = "cldr-cal-chinese-full"; loc = "zh"; file = "ca-chinese.json" },
    @{ pkg = "cldr-cal-chinese-full"; loc = "en"; file = "ca-chinese.json" }
)
$jsonRoot = "https://raw.githubusercontent.com/unicode-org/cldr-json/46.0.0/cldr-json"
foreach ($c in $calPackages) {
    $dir = Join-Path $DatesDir $c.loc
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $url = "$jsonRoot/$($c.pkg)/main/$($c.loc)/$($c.file)"
    $dest = Join-Path $dir $c.file
    try {
        Fetch-File $url $dest ""
    } catch {
        Write-Host "WARN skip $($c.loc)/$($c.file) : $_"
    }
}

# CLDR collation XML (Goal 4.9) — LDML rules / types
$CollDir = Join-Path $OutRoot "collation"
New-Item -ItemType Directory -Force -Path $CollDir | Out-Null
$collLocales = @(
    "root", "en", "de", "sv", "es", "tr", "ar", "fa", "ur", "he", "hi", "bn", "zh", "ja", "ko"
)
foreach ($loc in $collLocales) {
    $url = "https://raw.githubusercontent.com/unicode-org/cldr/release-46/common/collation/$loc.xml"
    $dest = Join-Path $CollDir "$loc.xml"
    try {
        Fetch-File $url $dest ""
    } catch {
        Write-Host "WARN skip collation $loc : $_"
    }
}

# Locale display names (Goal 4.10) — localenames + currencies + timeZoneNames + metaZones
$dispLocales = @(
    "und", "en", "en-GB", "ur", "ar", "ar-SA", "fa", "he", "hi", "bn", "th",
    "zh", "zh-Hans", "zh-Hant", "ja", "ko", "de", "fr", "es", "tr", "ru", "pl"
)
$jsonRoot = "https://raw.githubusercontent.com/unicode-org/cldr-json/46.0.0/cldr-json"
$LocalenamesDir = Join-Path $OutRoot "localenames"
New-Item -ItemType Directory -Force -Path $LocalenamesDir | Out-Null
$ldnFiles = @("languages.json", "scripts.json", "territories.json", "localeDisplayNames.json")
foreach ($loc in $dispLocales) {
    $dir = Join-Path $LocalenamesDir $loc
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    foreach ($f in $ldnFiles) {
        $url = "$jsonRoot/cldr-localenames-full/main/$loc/$f"
        $dest = Join-Path $dir $f
        try {
            Fetch-File $url $dest ""
        } catch {
            Write-Host "WARN skip localenames $loc/$f : $_"
        }
    }
    # Currencies (nested path used by generate_display.py)
    $numLocDir = Join-Path $NumDir $loc
    New-Item -ItemType Directory -Force -Path $numLocDir | Out-Null
    $urlCur = "$jsonRoot/cldr-numbers-full/main/$loc/currencies.json"
    $destCur = Join-Path $numLocDir "currencies.json"
    try {
        Fetch-File $urlCur $destCur ""
    } catch {
        Write-Host "WARN skip currencies $loc : $_"
    }
    # Time zone display names
    $dateLocDir = Join-Path $DatesDir $loc
    New-Item -ItemType Directory -Force -Path $dateLocDir | Out-Null
    $urlTz = "$jsonRoot/cldr-dates-full/main/$loc/timeZoneNames.json"
    $destTz = Join-Path $dateLocDir "timeZoneNames.json"
    try {
        Fetch-File $urlTz $destTz ""
    } catch {
        Write-Host "WARN skip timeZoneNames $loc : $_"
    }
}
$metaUrl = "$jsonRoot/cldr-core/supplemental/metaZones.json"
$metaDest = Join-Path $SuppDir "metaZones.json"
try {
    Fetch-File $metaUrl $metaDest ""
} catch {
    Write-Host "WARN skip metaZones.json : $_"
}

Write-Host "Fetched supplemental into $SuppDir"
Write-Host "Numbers in $NumDir; dates in $DatesDir; collation in $CollDir; localenames in $LocalenamesDir."
Write-Host "UCA DUCET: .\Fetch-Uca.ps1 then generate_uca.py / generate_collation.py"
Write-Host "Display names: python $Here\generate_display.py --write-golden"
Write-Host "Next: python $Here\generate_cldr.py --write-golden"
