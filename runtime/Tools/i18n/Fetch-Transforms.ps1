# Download curated CLDR 46 transform XML for RoboI18N Goal 4.25.
# Does NOT run during normal Build.ps1.

$ErrorActionPreference = "Stop"
$Here = $PSScriptRoot
$OutDir = Join-Path $Here "source\cldr-46.0.0\transforms"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$Base = "https://raw.githubusercontent.com/unicode-org/cldr/release-46/common/transforms"
$Files = @(
    "Cyrillic-Latin.xml",
    "Greek-Latin.xml",
    "Arabic-Latin.xml",
    "Hebrew-Latin.xml",
    "Devanagari-Latin.xml",
    "Devanagari-InterIndic.xml",
    "Bengali-Latin.xml",
    "Bengali-InterIndic.xml",
    "InterIndic-Latin.xml",
    "Latin-ASCII.xml",
    "Russian-Latin-BGN.xml",
    "Ukrainian-Latin-BGN.xml",
    "Hiragana-Latin.xml",
    "Hiragana-Katakana.xml",
    "Latin-Katakana.xml",
    "Hangul-Latin.xml",
    "Jamo-Latin.xml",
    "Latin-ConjoiningJamo.xml",
    "Any-Publishing.xml"
)

function Fetch-File([string]$Url, [string]$Dest) {
    Write-Host "GET $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing
    Write-Host "OK $([IO.Path]::GetFileName($Dest)) $((Get-Item $Dest).Length)"
}

foreach ($f in $Files) {
    Fetch-File "$Base/$f" (Join-Path $OutDir $f)
}

Write-Host "Transforms fetched into $OutDir"
