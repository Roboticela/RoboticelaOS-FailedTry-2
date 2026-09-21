[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$failed = 0
foreach ($step in @(
    "BuildBootloader.ps1",
    "BuildKernel.ps1",
    "BuildRuntime.ps1"
)) {
    Write-Host "==== $step ===="
    $script = Join-Path $PSScriptRoot $step
    $splat = @{ Configuration = $Configuration }
    if ($Verify) { $splat.Verify = $true }
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
