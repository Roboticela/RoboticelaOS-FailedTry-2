[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"

# Phase 12.3 -- the typography binding resolves a semantic role against the system typeface pack, so
# the pack must be present for the module's own suites to be meaningful: a build that compiled and
# tested against a missing pack would report a green typography module with nothing to resolve. The
# presence check is the pack's three required files, the same list `BuildFont.ps1` validates.
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$packRoot = Join-Path $repo "System\Fonts"
$required = @(
    (Join-Path $packRoot "system-fonts.manifest"),
    (Join-Path $packRoot "UI\Inter.ttf"),
    (Join-Path $packRoot "licenses\Inter-OFL.txt")
)
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw ("typography needs the system typeface pack, and it is incomplete: missing " + $file)
    }
}
Write-Host ("TYPO   system typeface pack: " + $packRoot)

$helper = Join-Path $PSScriptRoot "..\Utilities\Invoke-RoboRuntimeBuild.ps1"
& $helper -Module Typography -Configuration $Configuration -Verify:$Verify -Rebuild:$Rebuild
exit $LASTEXITCODE
