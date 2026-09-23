# The Latin & European shaping conformance suites (Phase 12.18), runnable on their own.
#
# `TestRoboText.ps1` runs everything under `text.`; this runs only `text.shaping.`, which is the corpus the
# phase's §46 asks be separable so that a shaping change does not have to be judged against the whole text
# layer's suite. It is the same build and the same binary — the `-Filter` is what narrows it — so a change that
# fails here fails there too, and nothing about the fixture (`Tests/Text/RoboTextConformanceFont.h`) is
# reachable from outside the text module's own tests.
#
# The full pass is still the phase's gate: a shaping change is not done until `TestRoboText.ps1` and the
# suites for every boundary it touched pass (`TestLayout`, `TestPaint`, `TestGraphics`, `TestFont`,
# `TestDesktop`, all `-Target Host`).
[CmdletBinding()]
param(
    [ValidateSet("Host", "Qemu")][string]$Target = "Host",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$helper = Join-Path $PSScriptRoot "..\Utilities\Invoke-RoboPartTest.ps1"
& $helper -Name "RoboTextConformance" -Target $Target -Configuration $Configuration -Verify:$Verify -Rebuild:$Rebuild `
    -Module "Text" -Filter "text.shaping."
exit $LASTEXITCODE
