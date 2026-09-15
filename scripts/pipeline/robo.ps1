# RoboticelaOS — the single entry point for building, testing, running and
# deploying the whole system.
#
#   scripts/robo check
#   scripts/robo build-all
#   scripts/robo test-all
#   scripts/robo validate-deployment
#   scripts/robo run-qemu
#
# This file owns argument parsing, command dispatch, result reporting and the
# process exit code. Everything else lives in the modules next to it:
#
#   config.ps1           paths, toolchain hints, QEMU settings, timeouts, flags
#   common.ps1           output, logging, process execution, result recording
#   engine.ps1           adapters over the bootloader/kernel/runtime sub-builds
#   commands.check.ps1   environment validation
#   commands.build.ps1   build commands
#   commands.test.ps1    test commands
#   commands.run.ps1     run commands
#   commands.deploy.ps1  deployment, validation and flashing

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Command = "",

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Rest = @()
)

$ErrorActionPreference = "Stop"

# A note on strict mode: this pipeline reads loosely-shaped probe results and
# JSON manifests, so it is deliberately not run under Set-StrictMode. Every
# value it depends on is checked explicitly at the point of use.

# --------------------------------------------------------------------------
# Module load
# --------------------------------------------------------------------------

. (Join-Path $PSScriptRoot "common.ps1")
. (Join-Path $PSScriptRoot "config.ps1")
. (Join-Path $PSScriptRoot "engine.ps1")
. (Join-Path $PSScriptRoot "commands.check.ps1")
. (Join-Path $PSScriptRoot "commands.build.ps1")
. (Join-Path $PSScriptRoot "commands.test.ps1")
. (Join-Path $PSScriptRoot "commands.run.ps1")
. (Join-Path $PSScriptRoot "commands.deploy.ps1")

# --------------------------------------------------------------------------
# Argument parsing
# --------------------------------------------------------------------------

$script:RoboUsageError = ""

function Get-RoboFlagSpec {
    <#
      Every option the pipeline accepts, in one table. `Kind` is one of
      switch/string/int; anything not listed here is a usage error rather than
      being silently ignored.
    #>
    return @{
        "-v"             = @{ Key = "Verbose"; Kind = "switch"; Help = "print each command as it runs" }
        "--verbose"      = @{ Key = "Verbose"; Kind = "switch"; Help = "print each command as it runs" }
        "--debug"        = @{ Key = "Debug"; Kind = "switch"; Help = "keep going after failures where possible" }
        "--rebuild"      = @{ Key = "Rebuild"; Kind = "switch"; Help = "ignore timestamps and rebuild everything" }
        "--skip-build"   = @{ Key = "SkipBuild"; Kind = "switch"; Help = "use the existing binaries" }
        "--skip-qemu"    = @{ Key = "SkipQemu"; Kind = "switch"; Help = "skip all QEMU acceptance steps" }
        "--headless"     = @{ Key = "Headless"; Kind = "switch"; Help = "no window; verify through markers" }
        "-h"             = @{ Key = "Help"; Kind = "switch"; Help = "show help" }
        "--help"         = @{ Key = "Help"; Kind = "switch"; Help = "show help" }
        "--yes"          = @{ Key = "AssumeYes"; Kind = "switch"; Help = "confirm a destructive operation" }
        "--force"        = @{ Key = "Force"; Kind = "switch"; Help = "allow flashing the system disk" }
        "--sanitize"     = @{ Key = "Sanitize"; Kind = "switch"; Help = "build the runtime with sanitizers" }
        "--baked"        = @{ Key = "IncludeBaked"; Kind = "switch"; Help = "clean also removes baked assets and fonts" }
        "--verbose-tests" = @{ Key = "VerboseTests"; Kind = "switch"; Help = "let the test binary print every case" }
        "--menu"         = @{ Key = "Menu"; Kind = "switch"; Help = "bootloader preview starts in the picker" }
        "--hold-alt"     = @{ Key = "HoldAlt"; Kind = "switch"; Help = "bootloader preview holds Alt" }
        "--fullscreen"   = @{ Key = "Visible"; Kind = "switch"; Help = "bootloader preview fullscreen" }
        "--visible"      = @{ Key = "Visible"; Kind = "switch"; Help = "show real windows; for test-display it runs the interactive host smoke" }
        "--firmware-menu" = @{ Key = "FirmwareMenu"; Kind = "switch"; Help = "pause the UEFI firmware in QEMU" }
        "--reset-nvram"  = @{ Key = "ResetNvram"; Kind = "switch"; Help = "reset the UEFI variable store" }
        "--quiet"        = @{ Key = "Quiet"; Kind = "switch"; Help = "suppress informational lines" }
        "--list"         = @{ Key = "ListCommands"; Kind = "switch"; Help = "list every command" }
        "--filter"       = @{ Key = "Filter"; Kind = "string"; Help = "only run runtime cases matching a filter" }
        "--configuration" = @{ Key = "Configuration"; Kind = "string"; Help = "Debug | Release" }
        "--device"       = @{ Key = "Device"; Kind = "string"; Help = "flash target (N, or \\.\PhysicalDriveN)" }
        "--esp-flags"    = @{ Key = "EspFlags"; Kind = "string"; Help = "extra bootloader flags baked into the image" }
        "--hold"         = @{ Key = "HoldMs"; Kind = "int"; Help = "demo window lifetime in milliseconds" }
        "--shot"         = @{ Key = "Shot"; Kind = "string"; Help = "write the demo frame as a PNG" }
        "--width"        = @{ Key = "Width"; Kind = "int"; Help = "preview window width" }
        "--height"       = @{ Key = "Height"; Kind = "int"; Help = "preview window height" }
        "--timeout"      = @{ Key = "TimeoutSeconds"; Kind = "int"; Help = "override the command timeout" }
    }
}

function Set-RoboOption {
    param([string]$Key, $Value)
    switch ($Key) {
        "Verbose" { $script:RoboFlag.Verbose = $true; $script:RoboVerbose = $true }
        "Debug" { $script:RoboFlag.Debug = $true }
        "Quiet" { $script:RoboQuiet = $true }
        "Help" { $script:RoboWantHelp = $true }
        "ListCommands" { $script:RoboWantList = $true }
        "Rebuild" { $script:RoboFlag.Rebuild = $true }
        "SkipBuild" { $script:RoboFlag.SkipBuild = $true }
        "SkipQemu" { $script:RoboFlag.SkipQemu = $true }
        "Headless" { $script:RoboFlag.Headless = $true }
        "AssumeYes" { $script:RoboFlag.AssumeYes = $true }
        "Force" { $script:RoboFlag.Force = $true }
        "Sanitize" { $script:RoboFlag.Sanitize = $true }
        "IncludeBaked" { $script:RoboFlag.IncludeBaked = $true }
        "VerboseTests" { $script:RoboFlag.VerboseTests = $true }
        "Menu" { $script:RoboFlag.Menu = $true }
        "HoldAlt" { $script:RoboFlag.HoldAlt = $true }
        "Visible" { $script:RoboFlag.Visible = $true }
        "FirmwareMenu" { $script:RoboFlag.FirmwareMenu = $true }
        "ResetNvram" { $script:RoboFlag.ResetNvram = $true }
        "Filter" { $script:RoboFlag.Filter = "$Value" }
        "Device" { $script:RoboFlag.Device = "$Value" }
        "EspFlags" { $script:RoboEspFlags = "$Value" }
        "Configuration" {
            if ($Value -notin @("Debug", "Release")) {
                $script:RoboUsageError = "--configuration must be Debug or Release (got '$Value')"
                return
            }
            $script:RoboConfig.RuntimeConfig = "$Value"
        }
        "HoldMs" { $script:RoboRun.HoldMs = [int]$Value }
        "Shot" { $script:RoboRun.Shot = "$Value" }
        "Width" { $script:RoboRun.Width = [int]$Value }
        "Height" { $script:RoboRun.Height = [int]$Value }
        "TimeoutSeconds" { $script:RoboTimeoutOverride = [int]$Value }
        default { $script:RoboUsageError = "unknown option '$Key'" }
    }
}

function Read-RoboArguments {
    <#
      Parses options, and accepts the command name in any position so both
      `robo run-qemu --headless` and `robo --headless run-qemu` work. Returns
      the command name (possibly empty).
    #>
    param([string[]]$Tokens)
    $spec = Get-RoboFlagSpec
    $command = ""
    for ($i = 0; $i -lt $Tokens.Count; $i++) {
        $tok = $Tokens[$i]
        if (-not $tok.StartsWith("-")) {
            if (-not $command) { $command = $tok; continue }
            $script:RoboUsageError = "unexpected argument '$tok' (one command at a time; see scripts/robo help)"
            return ""
        }
        if (-not $spec.ContainsKey($tok)) {
            $script:RoboUsageError = "unknown option '$tok' (see scripts/robo help)"
            return ""
        }
        $s = $spec[$tok]
        if ($s.Kind -eq "switch") {
            Set-RoboOption -Key $s.Key -Value $true
        } else {
            if ($i + 1 -ge $Tokens.Count) {
                $script:RoboUsageError = "$tok needs a value"
                return ""
            }
            $i++
            $val = $Tokens[$i]
            if ($s.Kind -eq "int") {
                $n = 0
                if (-not [int]::TryParse($val, [ref]$n)) {
                    $script:RoboUsageError = "$tok must be an integer (got '$val')"
                    return ""
                }
                Set-RoboOption -Key $s.Key -Value $n
            } else {
                Set-RoboOption -Key $s.Key -Value $val
            }
        }
        if ($script:RoboUsageError) { return "" }
    }
    return $command
}

# --------------------------------------------------------------------------
# Command table
# --------------------------------------------------------------------------

function New-RoboCommand {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][ValidateSet("meta", "build", "test", "run", "deploy")][string]$Group,
        [Parameter(Mandatory)][string]$Summary,
        [Parameter(Mandatory)][scriptblock]$Handler,
        [string]$Example = ""
    )
    return [ordered]@{ Name = $Name; Group = $Group; Summary = $Summary; Handler = $Handler; Example = $Example }
}

function Get-RoboCommands {
    $cmds = @()

    $cmds += New-RoboCommand -Name "check" -Group meta `
        -Summary "verify toolchain, firmware, sources and Chromium independence" `
        -Handler { Invoke-RoboCheck } -Example "scripts/robo check"

    $cmds += New-RoboCommand -Name "config" -Group meta `
        -Summary "print the resolved configuration" `
        -Handler { Show-RoboConfiguration; return $true }

    $cmds += New-RoboCommand -Name "clean" -Group meta `
        -Summary "remove all generated output (sources are never touched); --baked also clears baked assets" `
        -Handler { Invoke-RoboCleanCommand -IncludeSources:$script:RoboFlag.IncludeBaked; return $true }

    $cmds += New-RoboCommand -Name "rebuild" -Group meta `
        -Summary "clean, then a full build" `
        -Handler { Invoke-RoboRebuildCommand -EspFlags $script:RoboEspFlags; return $true }

    $cmds += New-RoboCommand -Name "version" -Group meta `
        -Summary "print pipeline, runtime and bootloader versions" `
        -Handler { Show-RoboVersions; return $true }

    # --- build ----------------------------------------------------------
    $cmds += New-RoboCommand -Name "build-all" -Group build `
        -Summary "build the entire system and publish the deployment artifacts" `
        -Handler { Invoke-RoboBuildAll -EspFlags $script:RoboEspFlags; return $true } `
        -Example "scripts/robo build-all"

    $cmds += New-RoboCommand -Name "build-assets" -Group build `
        -Summary "bake splash images and bootloader fonts" `
        -Handler { Invoke-RoboBuildAssets; return $true }

    $cmds += New-RoboCommand -Name "build-bootloader" -Group build `
        -Summary "host preview executable and UEFI BOOTX64.EFI" `
        -Handler { Invoke-RoboBuildBootloader; return $true }

    $cmds += New-RoboCommand -Name "build-kernel" -Group build `
        -Summary "kernel.elf (compiles libc, userland and packs initrd)" `
        -Handler { Invoke-RoboBuildKernelCommand; return $true }

    $cmds += New-RoboCommand -Name "build-userland" -Group build `
        -Summary "libc, user programs and the guest runtime ELFs" `
        -Handler { Invoke-RoboBuildUserlandCommand; return $true }

    $cmds += New-RoboCommand -Name "build-runtime" -Group build `
        -Summary "RoboticelaOS Runtime modules and host test binary" `
        -Handler { Invoke-RoboBuildRuntimeCommand; return $true }

    $cmds += New-RoboCommand -Name "build-image" -Group build `
        -Summary "EFI application, ESP and the bootable GPT disk image" `
        -Handler { Invoke-RoboBuildImageCommand -Flags $script:RoboEspFlags; Invoke-RoboBuildArtifactsCommand; return $true }

    # --- test -----------------------------------------------------------
    $cmds += New-RoboCommand -Name "test-all" -Group test `
        -Summary "every test surface: runtime, bare-metal host, QEMU acceptance" `
        -Handler { Invoke-RoboTestAll } -Example "scripts/robo test-all --skip-qemu"

    $cmds += New-RoboCommand -Name "test-runtime" -Group test `
        -Summary "build the runtime and run its whole host suite" `
        -Handler { Build-RoboRuntimeForTests; return (-not $script:RoboFailed) }

    $cmds += New-RoboCommand -Name "test-bootloader" -Group test `
        -Summary "bootloader unit + session tests" `
        -Handler { Invoke-RoboTestComponent -Component "bootloader"; return $true }

    $cmds += New-RoboCommand -Name "test-kernel" -Group test `
        -Summary "kernel host unit tests (memory, time, scheduler, ELF, VFS, ...)" `
        -Handler { Invoke-RoboTestComponent -Component "kernel"; return $true }

    $cmds += New-RoboCommand -Name "test-qemu-boot" -Group test `
        -Summary "QEMU: bootloader selftest under UEFI" `
        -Handler { Invoke-RoboTestComponent -Component "qemu-boot"; return $true }

    $cmds += New-RoboCommand -Name "test-qemu-kernel" -Group test `
        -Summary "QEMU: kernel and OS selftests under UEFI" `
        -Handler { Invoke-RoboTestComponent -Component "qemu-kernel"; return $true }

    $cmds += New-RoboCommand -Name "test-qemu-display" -Group test `
        -Summary "QEMU: the visual runtime rendering through the QEMU display backend" `
        -Handler { Invoke-RoboTestComponent -Component "qemu-display"; return $true }

    $cmds += New-RoboCommand -Name "test-e2e" -Group test `
        -Summary "clean build, boot, selftests, visual runtime, deployment validation" `
        -Handler { Invoke-RoboTestE2E } -Example "scripts/robo test-e2e"

    foreach ($component in (Get-RoboRuntimeComponentNames)) {
        $name = "test-$component"
        $summary = "runtime $component tests"
        $cmds += New-RoboCommand -Name $name -Group test -Summary $summary -Handler ([scriptblock]::Create(
                "Invoke-RoboTestComponent -Component '$component'; return `$true"))
    }

    # --- run ------------------------------------------------------------
    $cmds += New-RoboCommand -Name "run-bootloader" -Group run `
        -Summary "launch the bootloader host preview window" `
        -Handler { (Invoke-RoboRunBootloader) -eq 0 }

    $cmds += New-RoboCommand -Name "run-kernel" -Group run `
        -Summary "boot the real kernel in QEMU" `
        -Handler { (Invoke-RoboRunKernel) -eq 0 }

    $cmds += New-RoboCommand -Name "run-runtime" -Group run `
        -Summary "run the visual runtime as a native host window" `
        -Handler { (Invoke-RoboRunRuntime) -eq 0 } -Example "scripts/robo run-runtime"

    $cmds += New-RoboCommand -Name "run-runtime-qemu" -Group run `
        -Summary "boot the OS in QEMU and show the visual runtime" `
        -Handler { (Invoke-RoboRunRuntimeQemu) -eq 0 }

    $cmds += New-RoboCommand -Name "run-qemu" -Group run `
        -Summary "full build, then boot the OS in QEMU" `
        -Handler { (Invoke-RoboRunQemu) -eq 0 } -Example "scripts/robo run-qemu"

    # --- deploy ---------------------------------------------------------
    $cmds += New-RoboCommand -Name "deploy" -Group deploy `
        -Summary "produce the USB tree, disk image and USB archive" `
        -Handler { Invoke-RoboDeploy -EspFlags $script:RoboEspFlags; return $true }

    $cmds += New-RoboCommand -Name "validate-deployment" -Group deploy `
        -Summary "structurally validate the disk image and deployment tree" `
        -Handler { Invoke-RoboValidateDeployment }

    $cmds += New-RoboCommand -Name "flash" -Group deploy `
        -Summary "write the disk image to a physical device (needs --device and --yes)" `
        -Handler { (Invoke-RoboFlash) -eq 0 } -Example "scripts/robo flash --device 3 --yes"

    $cmds += New-RoboCommand -Name "create-robofs-image" -Group deploy `
        -Summary "create and inspect a standalone RoboFS image" `
        -Handler { Invoke-RoboCreateRobofsImage; return $true }

    return $cmds
}

function Invoke-RoboCreateRobofsImage {
    param([int]$Blocks = 256)
    Initialize-RoboOutputTree
    $py = Assert-RoboTool "python" "the RoboFS image tool is written in Python"
    $tool = Join-Path $script:RoboPaths.Tools "robofs_tool.py"
    if (-not (Test-Path -LiteralPath $tool)) { throw "robofs_tool.py not found: $tool" }
    $image = Join-Path $script:RoboOut.Images "robofs.img"
    foreach ($step in @("format", "inspect", "check")) {
        $cmdArgs = @($tool, $step, $image)
        if ($step -eq "format") { $cmdArgs += @("--blocks", "$Blocks") }
        $r = Invoke-RoboProcess -FilePath $py -Arguments $cmdArgs -TimeoutSeconds 120 -LogName "robofs-$step"
        if ($r.ExitCode -ne 0) {
            throw ("robofs $step failed (exit $($r.ExitCode))`n" + (Get-RoboTail $r.Output 30))
        }
        Write-RoboInfo ("robofs " + $step + " ok")
    }
    Write-RoboKeyValue "RoboFS image" $image
}

# --------------------------------------------------------------------------
# Help
# --------------------------------------------------------------------------

function Show-RoboHelp {
    param([object[]]$Commands)
    Write-Host ""
    Write-Host "  RoboticelaOS - build, test, run and deploy" -ForegroundColor White
    Write-Host "  -----------------------------------------" -ForegroundColor DarkGray
    Write-Host ""
    Write-Host "  scripts/robo <command> [options]" -ForegroundColor Gray
    Write-Host ""

    $groups = [ordered]@{
        "meta"   = "Inspect and maintain"
        "build"  = "Build"
        "test"   = "Test"
        "run"    = "Run"
        "deploy" = "Deploy"
    }
    foreach ($g in $groups.Keys) {
        $inGroup = @($Commands | Where-Object { $_.Group -eq $g })
        if ($inGroup.Count -eq 0) { continue }
        Write-Host ("  " + $groups[$g]) -ForegroundColor White
        foreach ($c in $inGroup) {
            Write-Host ("    " + $c.Name.PadRight(24)) -NoNewline -ForegroundColor Gray
            Write-Host $c.Summary -ForegroundColor DarkGray
        }
        Write-Host ""
    }

    Write-Host "  Options" -ForegroundColor White
    $seen = @{}
    foreach ($k in ((Get-RoboFlagSpec).Keys | Sort-Object)) {
        $s = (Get-RoboFlagSpec)[$k]
        if ($seen.ContainsKey($s.Key)) { continue }
        $seen[$s.Key] = $true
        $left = if ($s.Kind -eq "switch") { $k } else { "$k <value>" }
        Write-Host ("    " + $left.PadRight(24)) -NoNewline -ForegroundColor Gray
        Write-Host $s.Help -ForegroundColor DarkGray
    }
    Write-Host ""
    Write-Host "  Output" -ForegroundColor White
    Write-Host "    build/artifacts        bootloader, kernel, preview, tests" -ForegroundColor DarkGray
    Write-Host "    build/images           roboticelaos.img (GPT disk), robofs.img" -ForegroundColor DarkGray
    Write-Host "    build/dist             flash-ready USB/SD tree" -ForegroundColor DarkGray
    Write-Host "    build/logs             every command's full output" -ForegroundColor DarkGray
    Write-Host "    build/test-results     JSON + JUnit summaries" -ForegroundColor DarkGray
    Write-Host ""
    Write-Host "  Examples" -ForegroundColor White
    Write-Host "    scripts/robo check" -ForegroundColor DarkGray
    Write-Host "    scripts/robo build-all" -ForegroundColor DarkGray
    Write-Host "    scripts/robo test-all" -ForegroundColor DarkGray
    Write-Host "    scripts/robo validate-deployment" -ForegroundColor DarkGray
    Write-Host "    scripts/robo run-qemu" -ForegroundColor DarkGray
    Write-Host "    scripts/robo test-html" -ForegroundColor DarkGray
    Write-Host ""
}

function Show-RoboVersions {
    Write-RoboSection "Versions"
    $hdr = Join-Path $script:RoboPaths.Runtime "Include\RoboticelaRuntime.h"
    $runtimeVersion = "unknown"
    if (Test-Path -LiteralPath $hdr) {
        foreach ($line in Get-Content -LiteralPath $hdr) {
            if ($line -match 'ROBOTICELA_RUNTIME_VERSION_STRING\s+"([^"]+)"') { $runtimeVersion = $Matches[1]; break }
        }
    }
    $bootVersion = "unknown"
    $bhdr = Join-Path $script:RoboPaths.Bootloader "include\roboboot.h"
    if (Test-Path -LiteralPath $bhdr) {
        foreach ($line in Get-Content -LiteralPath $bhdr) {
            if ($line -match 'ROBO_VERSION_STRING\s+"([^"]+)"') { $bootVersion = $Matches[1]; break }
        }
    }
    Write-RoboKeyValue "Pipeline" "1.0.0"
    Write-RoboKeyValue "Runtime" $runtimeVersion
    Write-RoboKeyValue "Bootloader" $bootVersion
    Write-RoboKeyValue "Target" ("{0} / {1}" -f $script:RoboConfig.TargetArch, $script:RoboConfig.RuntimeConfig)
}

function Show-RoboCommands {
    param([object[]]$Commands)
    Write-RoboSection "Commands"
    foreach ($c in $Commands) {
        Write-Host ("  " + $c.Name.PadRight(24)) -NoNewline -ForegroundColor Gray
        Write-Host $c.Summary -ForegroundColor DarkGray
    }
}

# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

$script:RoboVerbose = $false
$script:RoboQuiet = $false
$script:RoboWantHelp = $false
$script:RoboWantList = $false
$script:RoboEspFlags = ""
$script:RoboTimeoutOverride = 0

$commands = Get-RoboCommands

$allTokens = @()
if ($Command) { $allTokens += $Command }
if ($Rest) { $allTokens += $Rest }
$commandName = Read-RoboArguments -Tokens $allTokens

if ($script:RoboUsageError) {
    Write-Host ""
    Write-RoboFail $script:RoboUsageError
    Write-RoboInfo "run 'scripts/robo help' for the accepted options"
    Write-Host ""
    exit 2
}

if ($script:RoboWantList) {
    Show-RoboCommands -Commands $commands
    exit 0
}

if (-not $commandName -or $commandName -eq "help" -or $script:RoboWantHelp) {
    Show-RoboHelp -Commands $commands
    exit 0
}

$match = @($commands | Where-Object { $_.Name -eq $commandName })
if ($match.Count -eq 0) {
    Write-Host ""
    Write-RoboFail "unknown command '$commandName'"
    Write-RoboInfo "run 'scripts/robo help' to see every command"
    Write-Host ""
    exit 2
}

$cmd = $match[0]

if ($script:RoboTimeoutOverride -gt 0) {
    $script:RoboTimeout.Build = $script:RoboTimeoutOverride
    $script:RoboTimeout.Bootloader = $script:RoboTimeoutOverride
    $script:RoboTimeout.Kernel = $script:RoboTimeoutOverride
    $script:RoboTimeout.Runtime = $script:RoboTimeoutOverride
    $script:RoboTimeout.Tests = $script:RoboTimeoutOverride
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()
Reset-RoboResults
Initialize-RoboOutputTree

if ($script:RoboFlag.Verbose) { Show-RoboConfiguration }

$isTestCommand = $cmd.Name -like "test-*"
$ok = $true
$exitCode = 0

try {
    $result = & $cmd.Handler
    if ($result -is [bool]) { $ok = $result }
    elseif ($null -ne $result -and $result -is [int]) { $exitCode = $result; $ok = ($result -eq 0) }
} catch {
    Write-Host ""
    Write-RoboFail ("$_")
    $ok = $false
    $exitCode = 1
    Add-RoboResult -Name $cmd.Name -Status fail -Layer "PIPELINE" -Detail "$_"
}

$sw.Stop()

if ($isTestCommand -or $cmd.Group -eq "test") {
    Show-RoboResultSummary -Name $cmd.Name
}

if ($script:RoboFailed) { $ok = $false }

Write-RoboBlank
if ($ok) {
    Write-Host ("  " + (Format-RoboDuration $sw.Elapsed.TotalSeconds) + "  " + $cmd.Name) -ForegroundColor DarkGray
    exit 0
}

Write-Host ("  " + (Format-RoboDuration $sw.Elapsed.TotalSeconds) + "  " + $cmd.Name + " - FAILED") -ForegroundColor Red
if ($exitCode -eq 0) { $exitCode = 1 }
exit $exitCode
