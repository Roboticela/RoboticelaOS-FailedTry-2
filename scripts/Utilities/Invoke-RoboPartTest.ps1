# Runs one part's tests. Build always happens on the host; -Target chooses where tests execute.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Name,
    [ValidateSet("Host", "Qemu")][string]$Target = "Host",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [switch]$Verify,
    [switch]$Rebuild,
    [string]$Module,
    [string]$Filter,
    [string]$PipelineHost,
    [string]$PipelineQemu
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

if ($Target -eq "Qemu") {
    if (-not $PipelineQemu) {
        Write-Host "$Name has no QEMU suite. Use -Target Host."
        exit 0
    }
    $robo = Join-Path $repo "scripts\pipeline\robo.ps1"
    $pipelineArgs = @($PipelineQemu)
    if ($Rebuild) { $pipelineArgs += "--rebuild" }
    & $robo @pipelineArgs
    exit $LASTEXITCODE
}

if ($PipelineHost) {
    $robo = Join-Path $repo "scripts\pipeline\robo.ps1"
    $pipelineArgs = @($PipelineHost)
    if ($Rebuild) { $pipelineArgs += "--rebuild" }
    & $robo @pipelineArgs
    exit $LASTEXITCODE
}

if (-not $Module -or -not $Filter) {
    throw "$Name host tests need -Module and -Filter"
}

$build = Join-Path $repo "Runtime\Tools\Build.ps1"
$splat = @{
    Configuration = $Configuration
    Module        = $Module
    SkipGuest     = $true
    BuildTests    = $true
}
if ($Verify) { $splat.Verify = $true }
if ($Rebuild) { $splat.Rebuild = $true }
& $build @splat
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$exe = Join-Path $repo "Build\Runtime\RoboTest.host.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "host test binary missing: $exe" }
& $exe "--filter=$Filter"
exit $LASTEXITCODE
