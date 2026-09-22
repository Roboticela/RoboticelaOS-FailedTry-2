[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"

# Phase 12.4 -- the text layer. It parses no font and keeps no Unicode database: faces come from
# RoboFont and script/bidi/encoding data from RoboUnicode (Phase 4), so what this script checks before
# building is that both of those inputs exist -- a text layer compiled and tested against a missing
# system typeface pack would report a green module with nothing to measure.
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$packRoot = Join-Path $repo "System\Fonts"
$required = @(
    (Join-Path $packRoot "system-fonts.manifest"),
    (Join-Path $packRoot "UI\Inter.ttf"),
    (Join-Path $packRoot "licenses\Inter-OFL.txt")
)
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw ("RoboText needs the system typeface pack, and it is incomplete: missing " + $file)
    }
}
Write-Host ("TEXT   system typeface pack: " + $packRoot)

$helper = Join-Path $PSScriptRoot "..\Utilities\Invoke-RoboRuntimeBuild.ps1"
& $helper -Module Text -Configuration $Configuration -Verify:$Verify -Rebuild:$Rebuild
exit $LASTEXITCODE
