[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"

# Phase 12.2 -- the system typeface pack lives in the repository's `System/Fonts/` and is installed
# into the image under `/System/Fonts/`. A build must never proceed without its system face, manifest
# or licence: a release image that silently shipped textless would be worse than a failed build.
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$packRoot = Join-Path $repo "System\Fonts"
$required = @(
    (Join-Path $packRoot "system-fonts.manifest"),
    (Join-Path $packRoot "UI\Inter.ttf"),
    (Join-Path $packRoot "licenses\Inter-OFL.txt")
)
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath $file)) {
        throw ("system font pack incomplete: missing " + $file)
    }
}
Write-Host ("FONT   system typeface pack: " + $packRoot)

$helper = Join-Path $PSScriptRoot "..\Utilities\Invoke-RoboRuntimeBuild.ps1"
& $helper -Module Font -Configuration $Configuration -Verify:$Verify -Rebuild:$Rebuild
exit $LASTEXITCODE
