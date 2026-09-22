[CmdletBinding()]
param(
    [ValidateSet("Host", "Qemu")][string]$Target = "Host",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$helper = Join-Path $PSScriptRoot "..\Utilities\Invoke-RoboPartTest.ps1"
# RoboText is a host-layer concern in this phase: it turns a string into runs, glyphs and a
# measurement, all of which are computed on the development machine and asserted in `text.*`. There is
# no QEMU-only behaviour in the module itself, and the guest-visible half of the phase is the desktop's
# own rendering check (`TestDesktop.ps1 -Target Qemu`), which boots the image and draws through the
# same pipeline. `-Target Qemu` is accepted so that the part can be driven the way every other part is,
# and it runs the same host suites.
& $helper -Name "RoboText" -Target $Target -Configuration $Configuration -Verify:$Verify -Rebuild:$Rebuild `
    -Module "Text" -Filter "text."
exit $LASTEXITCODE
