[CmdletBinding()]
param(
    [ValidateSet("Host", "Qemu")][string]$Target = "Host",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$dir = $PSScriptRoot
$failed = 0
$hostParts = @(
    "TestCore.ps1",
    "TestMemory.ps1",
    "TestContainers.ps1",
    "TestUnicode.ps1",
    "TestI18n.ps1",
    "TestFont.ps1",
    "TestTypography.ps1",
    "TestGraphics.ps1",
    "TestDisplay.ps1",
    "TestWindow.ps1",
    "TestInput.ps1",
    "TestHtml.ps1",
    "TestCss.ps1",
    "TestLayout.ps1",
    "TestPaint.ps1",
    "TestRasterizer.ps1",
    "TestDesktop.ps1",
    "TestKernel.ps1",
    "TestBootloader.ps1"
)
$qemuParts = @(
    "TestBootloader.ps1",
    "TestKernel.ps1",
    "TestDesktop.ps1"
)
$parts = if ($Target -eq "Qemu") { $qemuParts } else { $hostParts }
foreach ($step in $parts) {
    Write-Host "==== $step -Target $Target ===="
    $script = Join-Path $dir $step
    $splat = @{
        Target        = $Target
        Configuration = $Configuration
    }
    if ($Verify) { $splat.Verify = $true }
    if ($Rebuild) { $splat.Rebuild = $true }
    & $script @splat
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED $step"
        $failed = $LASTEXITCODE
        break
    }
}
exit $failed
