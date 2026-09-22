[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$failed = 0
$parts = @(
    "BuildLibc.ps1",
    "BuildCore.ps1",
    "BuildMemory.ps1",
    "BuildContainers.ps1",
    "BuildUnicode.ps1",
    "BuildI18n.ps1",
    "BuildFont.ps1",
    "BuildTypography.ps1",
    "BuildGraphics.ps1",
    "BuildDisplay.ps1",
    "BuildWindow.ps1",
    "BuildInput.ps1",
    "BuildHtml.ps1",
    "BuildCss.ps1",
    "BuildLayout.ps1",
    "BuildPaint.ps1",
    "BuildRasterizer.ps1",
    "BuildDesktop.ps1",
    "BuildRuntime.ps1",
    "BuildBootloader.ps1",
    "BuildKernel.ps1"
)
foreach ($step in $parts) {
    Write-Host "==== $step ===="
    $script = Join-Path $PSScriptRoot $step
    $splat = @{}
    if ($step -ne "BuildDesktopUi.ps1" -and $step -ne "BuildLibc.ps1" -and $step -ne "BuildBootloader.ps1" -and $step -ne "BuildKernel.ps1") {
        $splat.Configuration = $Configuration
        if ($Verify) { $splat.Verify = $true }
    }
    if ($Rebuild) { $splat.Rebuild = $true }
    & $script @splat
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED $step"
        $failed = $LASTEXITCODE
        break
    }
}
if ($failed -eq 0) {
    $robo = Join-Path $repo "scripts\pipeline\robo.ps1"
    & $robo "build-image"
    $failed = $LASTEXITCODE
}
exit $failed
