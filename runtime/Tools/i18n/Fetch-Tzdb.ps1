# Fetch IANA tzdb for RoboI18N Goal 4.14.
# Does NOT run during normal Build.ps1 (offline compile uses committed tables).

$ErrorActionPreference = "Stop"
$Here = $PSScriptRoot
$verLine = Select-String -Path (Join-Path $Here "robo_i18n_version.py") -Pattern '^TZDB_VERSION\s*=' | Select-Object -First 1
if (-not $verLine) { throw "TZDB_VERSION not found in robo_i18n_version.py" }
$TzdbVersion = ($verLine.Line -split '"')[1]
Write-Host "TZDB_VERSION=$TzdbVersion"

$SourceRoot = Join-Path $Here "source"
$OutDir = Join-Path $SourceRoot ("tzdb-" + $TzdbVersion)
New-Item -ItemType Directory -Force -Path $SourceRoot | Out-Null

$TzdataUrl = "https://data.iana.org/time-zones/releases/tzdata$TzdbVersion.tar.gz"
$TzcodeUrl = "https://data.iana.org/time-zones/releases/tzcode$TzdbVersion.tar.gz"
$TzdataGz = Join-Path $SourceRoot ("tzdata$TzdbVersion.tar.gz")
$TzcodeGz = Join-Path $SourceRoot ("tzcode$TzdbVersion.tar.gz")

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLower()
}

function Fetch-File([string]$Url, [string]$Dest) {
    if (Test-Path $Dest) {
        Write-Host "EXISTS $Dest"
        return
    }
    Write-Host "GET $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing
}

Fetch-File $TzdataUrl $TzdataGz
Fetch-File $TzcodeUrl $TzcodeGz
Write-Host "sha256 tzdata $(Get-Sha256 $TzdataGz)"
Write-Host "sha256 tzcode $(Get-Sha256 $TzcodeGz)"

if (Test-Path $OutDir) {
    Write-Host "Refreshing extract $OutDir (keeping zoneinfo if present)"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
# Extract data+code into OutDir (files at top level)
tar -xzf $TzdataGz -C $OutDir
tar -xzf $TzcodeGz -C $OutDir

$verFile = Join-Path $OutDir "version"
if (-not (Test-Path $verFile)) { throw "missing version file after extract" }
$got = (Get-Content $verFile -Raw).Trim()
if ($got -ne $TzdbVersion) {
    throw "tzdb version mismatch: expected $TzdbVersion got $got"
}
Write-Host "OK extracted tzdb $got"
Write-Host "Next: python generate_tzdb.py --write-golden"
Write-Host "Note: generate_tzdb.py copies TZif from Python tzdata package into zoneinfo/ if needed."
