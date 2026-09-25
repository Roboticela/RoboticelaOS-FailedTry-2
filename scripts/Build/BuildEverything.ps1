[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
. (Join-Path $PSScriptRoot "..\Utilities\RoboBuildProgress.ps1")

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
    "BuildRoboText.ps1",
    "BuildGraphics.ps1",
    "BuildCompositor.ps1",
    "BuildDisplay.ps1",
    "BuildWindow.ps1",
    "BuildInput.ps1",
    "BuildHtml.ps1",
    "BuildRoboJS.ps1",
    "BuildCss.ps1",
    "BuildLayout.ps1",
    "BuildPaint.ps1",
    "BuildRasterizer.ps1",
    "BuildDesktop.ps1",
    "BuildRuntime.ps1",
    "BuildBootloader.ps1",
    "BuildKernel.ps1"
)

if ($Rebuild) {
    Clear-RoboBuildEverythingState $repo
    Write-Host "RESUME cleared (-Rebuild): every part will run"
}

$state = Get-RoboBuildEverythingState -RepoRoot $repo -Configuration $Configuration
$completed = [System.Collections.Generic.List[string]]::new()
foreach ($c in @($state.completed)) {
    if ($c) { [void]$completed.Add([string]$c) }
}

$total = $parts.Count
$already = @($parts | Where-Object { $completed.Contains($_) }).Count
$left = $total - $already
Write-Host ("PLAN   BuildEverything: {0} parts | {1} already done | {2} left" -f $total, $already, $left)
if ($already -gt 0 -and -not $Rebuild) {
    Write-Host "RESUME continuing from Build\.build-everything.json (pass -Rebuild to start over)"
}

$index = 0
foreach ($step in $parts) {
    $index++
    $doneCount = $completed.Count
    $leftCount = $total - $doneCount
    Write-Host ("==== [{0}/{1}] {2}  (done {3} | left {4}) ====" -f $index, $total, $step, $doneCount, $leftCount)

    if ($completed.Contains($step)) {
        Write-Host ("RESUME skip {0} (already completed)" -f $step)
        continue
    }

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
        Save-RoboBuildEverythingState -RepoRoot $repo -Configuration $Configuration -Completed @($completed)
        Write-Host ("RESUME state saved ({0} completed). Re-run BuildEverything to continue." -f $completed.Count)
        $failed = $LASTEXITCODE
        break
    }
    [void]$completed.Add($step)
    Save-RoboBuildEverythingState -RepoRoot $repo -Configuration $Configuration -Completed @($completed)
}

if ($failed -eq 0) {
    Write-Host ("==== [image] build-image  (parts {0}/{0} done) ====" -f $total)
    $robo = Join-Path $repo "scripts\pipeline\robo.ps1"
    & $robo "build-image"
    $failed = $LASTEXITCODE
    if ($failed -eq 0) {
        Clear-RoboBuildEverythingState $repo
        Write-Host "DONE   BuildEverything: all parts complete (resume state cleared)"
    } else {
        Write-Host "FAILED build-image — part resume state kept so module builds are not repeated"
    }
}
exit $failed
