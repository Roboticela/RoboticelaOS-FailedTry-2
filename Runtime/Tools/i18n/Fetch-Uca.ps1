# Fetch Unicode Collation Algorithm (UCA) DUCET + CollationTest for Goal 4.9.
# Does NOT run during normal Build.ps1.

$ErrorActionPreference = "Stop"
$Here = $PSScriptRoot
$OutRoot = Join-Path $Here "source\uca-16.0.0"
New-Item -ItemType Directory -Force -Path $OutRoot | Out-Null

function Get-Sha256([string]$Path) {
    return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLower()
}

function Fetch-File([string]$Url, [string]$Dest) {
    Write-Host "GET $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing
    Write-Host "OK sha256 $(Get-Sha256 $Dest) $([IO.Path]::GetFileName($Dest))"
}

Fetch-File "https://www.unicode.org/Public/UCA/16.0.0/allkeys.txt" (Join-Path $OutRoot "allkeys.txt")
$zip = Join-Path $OutRoot "CollationTest.zip"
Fetch-File "https://www.unicode.org/Public/UCA/16.0.0/CollationTest.zip" $zip
$testDir = Join-Path $OutRoot "CollationTest"
if (Test-Path $testDir) { Remove-Item -Recurse -Force $testDir }
Expand-Archive -Path $zip -DestinationPath $testDir -Force
Write-Host "UCA 16.0.0 fetch complete → $OutRoot"
