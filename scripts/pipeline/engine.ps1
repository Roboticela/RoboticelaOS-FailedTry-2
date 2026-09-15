# RoboticelaOS pipeline — engine adapter.
#
# The pipeline does not reimplement compilation. It drives the sub-builds that
# already own that knowledge:
#
#   bootloader + kernel + libc + userland + ESP image  -> scripts/roboboot-build-common.ps1
#   QEMU process management                           -> scripts/roboboot-qemu-common.ps1
#   runtime modules, guest ELF, host test binary      -> runtime/Tools/Build.ps1
#
# Everything above this file (commands.*.ps1) talks only to the normalized
# wrappers declared here, so there is exactly one place that knows how each
# subsystem is actually invoked, where its outputs land, and how failure is
# detected.

. (Join-Path $PSScriptRoot "..\roboboot-qemu-common.ps1")

# --------------------------------------------------------------------------
# Output-location overrides
#
# The engines declare their own defaults; the pipeline redirects the *pure
# outputs* into the canonical build/ tree so a developer only has one place to
# look. Sub-project object directories are left alone: the toolchain and the
# incremental-compile checks reference them by design.
# --------------------------------------------------------------------------

$script:Dist = $script:RoboOut.Dist
$script:SerialLog = Join-Path $script:RoboOut.Temp "qemu-serial.log"
$script:OvmfDebug = Join-Path $script:RoboOut.Temp "ovmf-debug.log"
$script:VarsFd = Join-Path $script:RoboOut.Temp "ovmf-vars.fd"

# ESP image stays an engine output (QEMU boots it), but the pipeline also
# copies it to the canonical images/ path as the flash-drive artifact.
$script:RoboEngineImage = $script:EspImg

function Initialize-RoboEngine {
    Initialize-RoboTools
    Initialize-RoboOutputTree
    New-Item -ItemType Directory -Force -Path $script:RoboOut.Temp | Out-Null
    $script:RoboRebuild = [bool]$script:RoboFlag.Rebuild
}

function Get-RoboBootRoot { return $script:Boot }
function Get-RoboSerialLogPath { return $script:SerialLog }

# --------------------------------------------------------------------------
# Build wrappers
# --------------------------------------------------------------------------

function Invoke-RoboEngineAssets {
    Initialize-RoboEngine
    Invoke-RoboBake
}

function Invoke-RoboEngineBootloaderHost {
    Initialize-RoboEngine
    Push-Location $script:Boot
    try { Invoke-RoboBuildHost }
    finally { Pop-Location }
}

function Invoke-RoboEngineBootloaderTestBinary {
    Initialize-RoboEngine
    Push-Location $script:Boot
    try { Invoke-RoboBuildTest }
    finally { Pop-Location }
    Assert-RoboFile $script:TestOut "bootloader test binary" | Out-Null
}

function Invoke-RoboEngineBootloaderEfi {
    Initialize-RoboEngine
    Push-Location $script:Boot
    try { Invoke-RoboBuildEfi }
    finally { Pop-Location }
    Assert-RoboFile $script:EfiOut "UEFI bootloader" | Out-Null
}

function Invoke-RoboEngineUserland {
    Initialize-RoboEngine
    Push-Location $script:Boot
    try { Invoke-RoboBuildUserland }
    finally { Pop-Location }
}

function Invoke-RoboEngineKernel {
    Initialize-RoboEngine
    Push-Location $script:Boot
    try { Invoke-RoboBuildKernel }
    finally { Pop-Location }
    Assert-RoboFile $script:KernelOut "kernel ELF" | Out-Null
}

# The whole bare-metal chain: kernel (incl. userland + initrd) -> UEFI app ->
# ESP disk image -> USB deployment tree.
function Invoke-RoboEngineCombine {
    param([string]$Flags = "")
    Initialize-RoboEngine
    Push-Location $script:Boot
    try {
        Invoke-RoboCombine -Flags $Flags
    } finally { Pop-Location }
    Assert-RoboFile $script:EfiOut "UEFI bootloader" | Out-Null
    Assert-RoboFile $script:KernelOut "kernel ELF" | Out-Null
    Assert-RoboFile $script:RoboEngineImage "ESP disk image" | Out-Null
}

function Get-RoboDeploymentTree { return $script:Dist }

# --------------------------------------------------------------------------
# Host test wrappers
# --------------------------------------------------------------------------

# Bootloader host unit + session tests, then every kernel host test binary,
# then the libc host tests. This is the one call that covers the bare-metal
# side of the project.
function Invoke-RoboEngineHostTests {
    param([string]$LogName = "tests-bootloader")
    Initialize-RoboEngine
    Push-Location $script:Boot
    try {
        Invoke-RoboTestHost
    } finally { Pop-Location }
}

function Get-RoboKernelTestBinaries {
    <#
      The kernel host tests are individual executables. Enumerating them from
      the build directory means a newly added test binary is picked up by the
      pipeline without editing it.
    #>
    $kbuild = Join-Path $script:KernelRoot "build"
    if (-not (Test-Path -LiteralPath $kbuild)) { return @() }
    return @(Get-ChildItem -LiteralPath $kbuild -Filter "test_*.exe" -File -ErrorAction SilentlyContinue |
        Sort-Object Name | ForEach-Object { $_.FullName })
}

# --------------------------------------------------------------------------
# Runtime (RoboticelaOS Runtime) wrappers
# --------------------------------------------------------------------------

function Get-RoboRuntimeBuildScript { return (Join-Path $script:RoboPaths.Runtime "Tools\Build.ps1") }
function Get-RoboRuntimeTestBinary { return (Join-Path $script:RoboOut.Runtime "RoboTest.host.exe") }

function Invoke-RoboEngineRuntime {
    <#
      Runs runtime/Tools/Build.ps1 as a child PowerShell so its exit code,
      output and timeouts behave like any other real build step.
    #>
    param(
        [ValidateSet("Debug", "Release")][string]$Configuration = "",
        [switch]$Verify,
        [switch]$Test,
        [switch]$BuildTests,
        [switch]$Sanitize,
        [string]$Filter = "",
        [switch]$Clean,
        [switch]$VerboseTests,
        [string]$LogName = "build-runtime",
        [switch]$Continue
    )
    if (-not $Configuration) { $Configuration = $script:RoboConfig.RuntimeConfig }
    $scriptPath = Get-RoboRuntimeBuildScript
    if (-not (Test-Path -LiteralPath $scriptPath)) { throw "runtime build script missing: $scriptPath" }

    $cmdArgs = @("-NoProfile", "-File", $scriptPath, "-Configuration", $Configuration)
    if ($Clean) { $cmdArgs += "-Clean" }
    if ($Verify) { $cmdArgs += "-Verify" }
    if ($Test) { $cmdArgs += "-Test" }
    if ($BuildTests) { $cmdArgs += "-BuildTests" }
    if ($Sanitize) { $cmdArgs += "-Sanitize" }
    if ($VerboseTests) { $cmdArgs += "-VerboseTests" }
    if ($Filter) { $cmdArgs += @("-Filter", $Filter) }

    $name = "runtime build"
    $detail = @()
    $detail += $Configuration
    if ($Verify) { $detail += "verify" }
    $detail += $(if ($Test) { if ($Filter) { "test:$Filter" } else { "test" } }
                 elseif ($BuildTests) { "tests built, not run" }
                 else { "no tests" })
    if ($Sanitize) { $detail += "sanitize" }
    $name += " (" + ($detail -join ", ") + ")"

    $ok = Invoke-RoboStep -Name $name -Layer "BUILD" -Continue:$Continue -Action {
        $pwsh = Get-RoboPowerShell
        $r = Invoke-RoboProcess -FilePath $pwsh -Arguments $cmdArgs `
            -WorkingDirectory $script:RoboPaths.Runtime -TimeoutSeconds $script:RoboTimeout.Build -LogName $LogName
        if ($r.TimedOut) { throw ("runtime build timed out after $($script:RoboTimeout.Build)s") }
        if ($r.ExitCode -ne 0) {
            throw ("runtime build failed with exit code $($r.ExitCode) " +
                   "(log: $(Get-RoboLogPath $LogName))`n" + (Get-RoboTail $r.Output 60))
        }
    }
    return $ok
}

function Get-RoboRuntimeTestList {
    <#
      Asks the real test binary for its suite names so `test-all`'s coverage
      claim is derived from the binary, not from a hand-maintained list.
    #>
    param([string]$Configuration = "")
    if (-not $Configuration) { $Configuration = $script:RoboConfig.RuntimeConfig }
    $exe = Get-RoboRuntimeTestBinary
    if (-not (Test-Path -LiteralPath $exe)) { return @() }
    $r = Invoke-RoboProcess -FilePath $exe -Arguments @("--list") `
        -WorkingDirectory $script:RoboOut.Runtime -TimeoutSeconds 60
    if ($r.ExitCode -ne 0) { return @() }
    $names = @()
    foreach ($line in ($r.Output -split "`r?`n")) {
        $t = $line.Trim()
        if ($t -match '^[a-z][a-z0-9_]*(\.[a-z0-9_]+)+$') { $names += $t }
    }
    return $names
}

function Get-RoboRuntimeTestGroups {
    <#
      Maps the runtime's test namespaces onto the pipeline's component names.
      Only groups that exist in the built binary are returned.
    #>
    param([string[]]$Filters = @())
    if ($Filters.Count -eq 0) {
        $Filters = @(
            "base", "error", "log", "debug", "mem", "memdbg", "memsys", "arena", "pool",
            "array", "string", "unicode", "ucd", "norm", "case", "grapheme", "word",
            "sentence", "line", "bidi", "emoji", "infra", "i18n", "font", "script",
            "graphics", "window", "display", "html", "demo"
        )
    }
    $all = @(Get-RoboRuntimeTestList)
    $groups = @()
    foreach ($f in $Filters) {
        $hits = @($all | Where-Object { $_ -like "$f.*" })
        if ($hits.Count -gt 0) {
            $groups += [ordered]@{ Filter = "$f."; Cases = $hits.Count }
        }
    }
    return $groups
}

# --------------------------------------------------------------------------
# QEMU
# --------------------------------------------------------------------------

function Get-RoboQemuArgs {
    <#
      The single QEMU argument builder for the whole project. The machine is
      q35 + OVMF because the OS boots through UEFI; the ESP image is attached
      as the boot disk; a spare virtio disk and the host loopback port-forwards
      exist because the kernel's own tests exercise storage and networking.
    #>
    param(
        [switch]$Headless,
        [switch]$ResetNvram,
        [switch]$FirmwareMenu
    )
    $code = Get-OvmfCode
    New-OvmfVars -Reset:($ResetNvram -or $Headless)

    $cmdArgs = @()
    $display = if ($Headless) { $script:RoboQemu.HeadlessDisplay } else { $script:RoboQemu.Display }

    $boot = if ($FirmwareMenu) { "order=c,menu=on,splash-time=8000" } else { "order=c,menu=off,splash-time=0" }
    $cmdArgs += @(
        "-machine", $script:RoboQemu.Machine,
        "-cpu", $script:RoboQemu.Cpu,
        "-m", "$($script:RoboQemu.MemoryMB)",
        "-vga", $script:RoboQemu.Vga,
        "-netdev", ("user,id=n0,net=10.0.2.0/24,dhcpstart=10.0.2.15," +
                    "guestfwd=tcp:10.0.2.4:$($script:RoboQemu.NetPorts[0])-tcp:127.0.0.1:$($script:RoboQemu.NetPorts[0])," +
                    "guestfwd=tcp:10.0.2.4:$($script:RoboQemu.NetPorts[1])-tcp:127.0.0.1:$($script:RoboQemu.NetPorts[1])"),
        "-device", "virtio-net-pci,netdev=n0,disable-legacy=on",
        "-boot", $boot,
        "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=$code",
        "-drive", "if=pflash,format=raw,unit=1,file=$script:VarsFd",
        "-fw_cfg", "name=opt/org.tianocore/EFIShellSupport,string=no",
        "-serial", "file:$script:SerialLog"
    )
    if (-not $FirmwareMenu) {
        $cmdArgs += @("-fw_cfg", "name=opt/org.tianocore/FirmwareSetupSupport,string=no")
    }
    if ($script:RoboQemu.DiskMode -eq "virtio") {
        $cmdArgs += @(
            "-drive", "file=$script:RoboEngineImage,format=raw,if=none,id=esp,cache=writethrough",
            "-device", "virtio-blk-pci,drive=esp,bootindex=1"
        )
    } else {
        $cmdArgs += @("-drive", "file=$script:RoboEngineImage,format=raw,if=ide,index=0,media=disk,cache=writethrough")
    }

    $vblk = Join-Path $script:RoboOut.Temp "virtio-test.img"
    if (-not (Test-Path -LiteralPath $vblk)) {
        $fs = [System.IO.File]::Open($vblk, 'Create', 'Write')
        $fs.SetLength(8MB)
        $fs.Close()
    }
    $cmdArgs += @(
        "-drive", "file=$vblk,format=raw,if=none,id=vblk,cache=writethrough",
        "-device", "virtio-blk-pci,disable-legacy=on,drive=vblk"
    )

    if ($Headless) {
        $cmdArgs += @("-display", "none", "-no-reboot")
    } else {
        $cmdArgs += @("-display", $display)
    }
    return $cmdArgs
}

function Read-RoboSerial {
    if (-not (Test-Path -LiteralPath $script:SerialLog)) { return "" }
    try {
        $fs = [System.IO.File]::Open($script:SerialLog,
            [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        try {
            $sr = New-Object System.IO.StreamReader($fs)
            return $sr.ReadToEnd()
        } finally { $fs.Dispose() }
    } catch { return "" }
}

function Reset-RoboSerialLog {
    if (Test-Path -LiteralPath $script:SerialLog) {
        Remove-Item -LiteralPath $script:SerialLog -Force -ErrorAction SilentlyContinue
    }
}

function Wait-RoboSerialMarkers {
    <#
      Waits until every pattern in -Markers matched, or a -FailPattern
      appeared, or the process died, or the timeout fired.

      Returns:
        Complete  - all markers seen
        Failed    - a fail pattern appeared
        Missing   - markers that never appeared
        Serial    - the captured serial text
        Seconds   - wall time
    #>
    param(
        [Parameter(Mandatory)][string[]]$Markers,
        [int]$TimeoutSeconds = 180,
        [string[]]$FailPatterns = @(),
        [System.Diagnostics.Process]$Process = $null,
        [int]$PollMilliseconds = 300,
        [scriptblock]$Progress = $null
    )
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $serial = ""
    $failedBy = ""
    while ((Get-Date) -lt $deadline) {
        $serial = Read-RoboSerial
        $failedBy = ""
        foreach ($p in $FailPatterns) {
            if ($serial -match $p) { $failedBy = $p; break }
        }
        if ($failedBy) { break }
        $missing = @($Markers | Where-Object { $serial -notmatch $_ })
        if ($missing.Count -eq 0) { break }
        if ($Process -and $Process.HasExited) { break }
        if ($Progress) { & $Progress $serial }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
    $sw.Stop()
    $serial = Read-RoboSerial
    $missing = @($Markers | Where-Object { $serial -notmatch $_ })
    $failedBy = ""
    foreach ($p in $FailPatterns) {
        if ($serial -match $p) { $failedBy = $p; break }
    }
    return @{
        Complete = ($missing.Count -eq 0) -and (-not $failedBy)
        Failed   = [bool]$failedBy
        FailedBy = $failedBy
        Missing  = $missing
        Serial   = $serial
        Seconds  = [Math]::Round($sw.Elapsed.TotalSeconds, 2)
    }
}

function Start-RoboQemu {
    param(
        [string[]]$Arguments,
        [switch]$Headless
    )
    $qemu = Assert-RoboTool "qemu" "QEMU is required to run the OS"
    Initialize-RoboEngine
    if (Test-Path -LiteralPath $script:SerialLog) { Remove-Item -LiteralPath $script:SerialLog -Force -ErrorAction SilentlyContinue }
    return (Start-QemuProcess -Arguments $Arguments -Accel $script:RoboQemu.Accel -Hidden:$Headless)
}

function Stop-RoboQemu {
    param([System.Diagnostics.Process]$Process)
    if ($Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
        try { $Process.WaitForExit(5000) | Out-Null } catch {}
    }
}

# --------------------------------------------------------------------------
# Host process / shell helpers
# --------------------------------------------------------------------------

function Get-RoboPowerShell {
    # Relaunching the pipeline in a child PowerShell needs the same executable
    # that is running this one.
    if ($PSVersionTable.PSEdition -eq "Core") {
        $p = (Get-Process -Id $PID).Path
        if ($p) { return $p }
        $c = Get-Command pwsh -ErrorAction SilentlyContinue
        if ($c) { return $c.Source }
    }
    $c = Get-Command powershell -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    return (Get-Process -Id $PID).Path
}

function Test-RoboWindowsHost {
    return ($env:OS -eq "Windows_NT") -or ($PSVersionTable.Platform -eq "Win32NT")
}

function Assert-RoboWindowsHost {
    if (-not (Test-RoboWindowsHost)) {
        throw ("this project's build targets a Windows development host " +
               "(clang/LLVM, lld-link, UEFI). Detected: " +
               "$($PSVersionTable.OS). Use Git Bash or PowerShell on Windows.")
    }
}
