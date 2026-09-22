# RoboticelaOS pipeline — test commands.
#
# Three test surfaces exist and each is exercised for real:
#
#   1. bare-metal host tests  - bootloader session tests + kernel unit tests +
#                               libc tests, compiled and executed on the host
#   2. runtime host tests     - RoboTest.host.exe, grouped by subsystem
#   3. QEMU acceptance        - the real OS booting under UEFI, checked against
#                               markers the OS itself prints
#
# `test-all` runs all three (QEMU skipped automatically when unavailable) and
# reports a non-zero exit if anything failed.

# --------------------------------------------------------------------------
# Runtime component groups
#
# Names here are user-facing: they are what the developer sees and what the
# `test-<component>` commands accept. The filter lists are matched against the
# suite names the test binary actually reports.
# --------------------------------------------------------------------------

$script:RoboRuntimeComponents = [ordered]@{
    "base"        = @("base.", "error.", "log.", "debug.")
    "memory"      = @("mem.", "memdbg.", "memsys.", "arena.", "pool.")
    "containers"  = @("array.", "string.")
    "unicode"     = @("unicode.", "ucd.", "norm.", "case.", "grapheme.", "word.",
                      "sentence.", "line.", "bidi.", "emoji.")
    "i18n"        = @("i18n.")
    "font"        = @("font.")
    "script"      = @("script.")
    "graphics"    = @("graphics.")
    "window"      = @("window.")
    "display"     = @("display.")
    "html"        = @("html.")
    "css"         = @("css.")
    # RoboLayout's own suites live in runtime/layout/tests and report under
    # `layout.`.
    "layout"      = @("layout.")
    # RoboPaint's suites live in runtime/paint/tests and report under `paint.`.
    "paint"       = @("paint.")
    # RoboRender and RoboDesktop: the rasteriser, and the shell that composites what
    # it produces (the desktop group also carries the checkpoint-scene cases).
    "render"      = @("render.")
    "desktop"     = @("desktop.")
    "input"       = @("input.")
    "infra"       = @("infra.")
}

function Get-RoboRuntimeComponentNames {
    return @($script:RoboRuntimeComponents.Keys)
}

# --------------------------------------------------------------------------
# Runtime host tests
# --------------------------------------------------------------------------

function Build-RoboRuntimeForTests {
    <#
      Builds the runtime and its host test binary. With the whole suite enabled
      (the default, and what `test-runtime` and `test-all` want) the runtime
      build script runs every case as part of -Test, so one invocation is both
      the build and the whole-suite gate; the per-component runs afterwards only
      exist to say which subsystem failed.

      A focused command such as `test-html` passes -BuildOnly: it must build the
      binary without gating on suites it does not own, otherwise an unrelated
      failure (a host window suite with no foreground session) would be reported
      against the component the developer actually asked about.
    #>
    param([switch]$Continue, [switch]$BuildOnly)
    if ($script:RoboFlag.SkipBuild -and (Test-Path -LiteralPath (Get-RoboRuntimeTestBinary))) { return }

    # The runtime build treats libc.a and crt0/crti/crtn as required inputs, so a
    # tree that has just been cleaned needs the bare-metal userland step first.
    # Only pay for it when the archive is genuinely absent: the step is
    # incremental, but there is no reason to run it on every test command.
    $libcArchive = Join-Path $script:RoboOut.Libc "libc.a"
    if (-not (Test-Path -LiteralPath $libcArchive)) {
        Write-RoboInfo ("libc.a missing - building the bare-metal prerequisites first")
        Invoke-RoboBuildUserlandCommand
    }

    if ($BuildOnly) {
        Invoke-RoboEngineRuntime -Configuration $script:RoboConfig.RuntimeConfig -Verify -BuildTests `
            -Continue:$Continue -LogName "build-runtime" | Out-Null
    } else {
        Invoke-RoboEngineRuntime -Configuration $script:RoboConfig.RuntimeConfig -Verify -Test `
            -Continue:$Continue -LogName "build-runtime" | Out-Null
    }
}

function Invoke-RoboRuntimeTestFilter {
    <#
      Runs the real test binary with one filter and turns its exit code plus
      its own summary lines into a recorded result.
    #>
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Filter,
        [switch]$Continue
    )
    Invoke-RoboStep -Name $Name -Layer "TEST" -Continue:$Continue -Action {
        $exe = Get-RoboRuntimeTestBinary
        Assert-RoboFile $exe "runtime test binary" | Out-Null
        $log = "runtime-" + ($Filter.TrimEnd(".") -replace "[^a-zA-Z0-9_]", "_")
        $r = Invoke-RoboProcess -FilePath $exe -Arguments @("--filter=$Filter") `
            -WorkingDirectory $script:RoboOut.Runtime `
            -TimeoutSeconds $script:RoboTimeout.Tests -LogName $log
        if ($r.TimedOut) { throw ("runtime tests timed out after $($script:RoboTimeout.Tests)s") }
        if ($r.ExitCode -ne 0) {
            throw ("filter '$Filter' failed (exit $($r.ExitCode)), log: $(Get-RoboLogPath $log)`n" +
                   (Get-RoboTail $r.Output 50))
        }
        $summary = ($r.Output -split "`r?`n" | Where-Object { $_ -match "passed|failed|ALL TESTS" } |
            Select-Object -Last 1)
        if ($summary) { Write-RoboInfo $summary.Trim() }
    } | Out-Null
}

function Invoke-RoboRuntimeComponentTest {
    param(
        [Parameter(Mandatory)][string]$Component,
        [switch]$Continue
    )
    if (-not $script:RoboRuntimeComponents.Contains($Component)) {
        throw ("unknown runtime component '$Component'. Known: " +
               ((Get-RoboRuntimeComponentNames) -join ", "))
    }
    $filters = @($script:RoboRuntimeComponents[$Component])
    # The window/display suites include an interactive host smoke that is only
    # meaningful when a desktop session exists; --visible turns it on.
    if ($script:RoboFlag.Visible) { $env:ROBO_WINDOW_SMOKE = "1" }
    try {
        Invoke-RoboStep -Name ("runtime: " + $Component) -Layer "TEST" -Continue:$Continue -Action {
            $exe = Get-RoboRuntimeTestBinary
            Assert-RoboFile $exe "runtime test binary" | Out-Null
            foreach ($f in $filters) {
                $r = Invoke-RoboProcess -FilePath $exe -Arguments @("--filter=$f") `
                    -WorkingDirectory $script:RoboOut.Runtime -TimeoutSeconds $script:RoboTimeout.Tests
                if ($r.TimedOut) { throw ("filter '$f' timed out") }
                if ($r.ExitCode -ne 0) {
                    throw ("filter '$f' failed (exit $($r.ExitCode))`n" + (Get-RoboTail $r.Output 50))
                }
            }
            Write-RoboInfo ("filters: " + ($filters -join " "))
        } | Out-Null
    } finally {
        if ($script:RoboFlag.Visible) { Remove-Item Env:ROBO_WINDOW_SMOKE -ErrorAction SilentlyContinue }
    }
}

function Invoke-RoboRuntimeDiscoverCoverage {
    <#
      Derives the component groups from the test binary itself so a new
      subsystem is tested by `test-all` without editing this file, and records
      any group that no component filter claims (so nothing is silently
      untested).
    #>
    $all = @(Get-RoboRuntimeTestList)
    if ($all.Count -eq 0) { return @{ Groups = @(); Total = 0; Unclaimed = @() } }
    $claimed = @()
    foreach ($v in $script:RoboRuntimeComponents.Values) { $claimed += $v }

    $prefixes = @{}
    foreach ($t in $all) {
        $p = ($t -split "\.")[0] + "."
        if ($prefixes.ContainsKey($p)) { $prefixes[$p]++ } else { $prefixes[$p] = 1 }
    }
    $unclaimed = @()
    foreach ($p in ($prefixes.Keys | Sort-Object)) {
        if ($claimed -notcontains $p) { $unclaimed += @{ Prefix = $p; Count = $prefixes[$p] } }
    }
    return @{ Groups = $prefixes; Total = $all.Count; Unclaimed = $unclaimed }
}

# --------------------------------------------------------------------------
# Bare-metal host tests
# --------------------------------------------------------------------------

function Invoke-RoboBareHostTests {
    param([switch]$Continue)

    # Building the whole host test set is one step; it is the engine's own
    # aggregate (bootloader tests + every kernel unit test + libc).
    Invoke-RoboStep -Name "build bare-metal host tests" -Layer "TEST" -Continue:$Continue -Action {
        Push-Location $script:Boot
        try { Invoke-RoboTestHost }
        finally { Pop-Location }
    } | Out-Null

    # Then report each kernel unit test individually so a failure names itself,
    # and so the granular results are visible in the JSON/JUnit output.
    $bins = @(Get-RoboKernelTestBinaries)
    if ($bins.Count -eq 0) {
        Add-RoboResult -Name "kernel unit tests" -Status skip -Layer "TEST" `
            -Detail "no test_*.exe produced (build step did not complete)"
        Write-RoboSkipped "kernel unit tests (no binaries)"
        return
    }
    foreach ($bin in $bins) {
        $leaf = Split-Path $bin -Leaf
        Invoke-RoboRecordedCommand -Name ("kernel: " + ($leaf -replace "^test_", "" -replace "\.exe$", "")) `
            -Layer "TEST" -FilePath $bin -WorkingDirectory $script:Boot `
            -TimeoutSeconds $script:RoboTimeout.Tests -Continue:$Continue | Out-Null
    }
}

# --------------------------------------------------------------------------
# QEMU acceptance
# --------------------------------------------------------------------------

# The `ktest` boot flag makes the bootloader pass `selftest` to the kernel, which
# turns on the kernel's own suites AND the user-space selftest services. Both are
# real gates, but they are separate gates: a broken kernel must not be reported as
# a broken window server and vice versa. Hence two marker sets.

# Kernel + OS core. Every marker here is produced by kernel code.
$script:RoboKernelBootMarkers = @(
    "Roboticela Kernel",
    "Boot protocol: Roboticela",
    "CPU_TEST_OK", "PMM_TEST_OK", "VMM_TEST_OK", "HEAP_TEST_OK", "PF_TEST_OK",
    "TIMER_TEST_OK", "CTX_TEST_OK", "SCHED_TEST_OK", "THREAD_TEST_OK", "PROC_TEST_OK",
    "KERNEL PANIC", "Page Fault", "Present : NO", "Access  : READ", "Mode    : KERNEL",
    "Hello from user space\.",
    "\[PASS\] ROBOTICELA OS PHASE 10",
    "\[PASS\] ROBOTICELA OS PHASE 11",
    "\[PASS\] ROBOTICELA OS FILE DESCRIPTORS",
    "\[PASS\] ROBOTICELA OS VFS",
    "\[PASS\] ROBOFS v1",
    "Hello from RoboFS\.",
    "USER_ROBOFS_OK",
    "\[PASS\] DRIVER SYSTEM",
    "\[PASS\] STORAGE DRIVER",
    # The kernel's network marker is "NETWORK STACK". Bare "NETWORK" belongs to
    # the guest's nettest program - matching it here would both credit the kernel
    # with a user-space verdict and match it by accident, as a substring.
    "\[PASS\] NETWORK STACK"
)

# The kernel prints its own verdict for every suite it runs; any of these means a
# kernel regression and must fail the boot even though the run reached the end.
$script:RoboKernelFailPatterns = @(
    "\[FAIL\] ROBOTICELA OS PHASE 10",
    "\[FAIL\] ROBOTICELA OS PHASE 11",
    "\[FAIL\] ROBOTICELA OS FILE DESCRIPTORS",
    "\[FAIL\] ROBOTICELA OS VFS",
    "\[FAIL\] ROBOFS v1",
    "\[FAIL\] DRIVER SYSTEM",
    "\[FAIL\] STORAGE DRIVER",
    # kernel/net/core/network.c prints this when there is no interface at all.
    "\[FAIL\] NETWORK  no interface",
    "KERNEL PANIC\s*$\r?\n\s*Kernel halted",
    "USER_ROBOFS_FAIL"
)

# User-space selftest services (window-test, compositor-test, nettest, libc-test,
# init). These are separate programs, so they are a separate gate.
#
# Two of these markers are currently unreachable and must stay in this gate rather
# than the kernel one, because the components they test do not exist yet:
#   [PASS] RoboCompositor - user/init/config.c starts no compositor service
#   [PASS] INIT PROCESS  - init only prints its summary once every service has
#                          exited, but robo-runtime runs for the whole boot and
#                          ends the boot by shutting the display down
# The gate failing is the honest report: these components are not implemented.
$script:RoboUserSpaceMarkers = @(
    "\[PASS\] Window Server",
    "\[PASS\] RoboCompositor",
    "\[PASS\] INIT PROCESS",
    "\[PASS\] LIBC",
    "\[PASS\] NETWORK"
)

# The guest reports its own failures by name; surface them as the gate failure.
$script:RoboUserSpaceFailPatterns = @(
    "\[FAIL\] LIBC",
    "\[FAIL\] Window Server",
    "\[FAIL\] RoboCompositor",
    "\[FAIL\] INIT PROCESS",
    # user/nettest/main.c - the guest's own network client test
    "\[FAIL\] NETWORK\r?$"
)

#
# The display smoke test requires the guest's own checkpoint, not just a picture:
# the runtime program runs the shared desktop scene on the framebuffer and reports
# what each stage of the chain did. The counters are asserted by value, because a
# shell whose text stopped reaching the picture -- a face that did not load, a
# document that stopped producing runs -- must fail here rather than pass as a
# window with something in it.
#
$script:RoboDisplayMarkers = @(
    # The display came up, and at the mode the checkpoint is written for.
    "ROBO_QEMU_DISPLAY_OK",
    "ROBO_QEMU_FB_W=0x00000500",
    "ROBO_QEMU_FB_H=0x00000320",
    # The system face was read from the image and parsed; without it every label below is shapes-not-text.
    "ROBO_QEMU_FACE_OK",
    "ROBO_QEMU_DESKTOP_OK",
    "ROBO_QEMU_DESKTOP_WINDOW=Welcome",
    "ROBO_QEMU_DESKTOP_STATUS=1280x800",
    # What the frame says. These are the *same numbers the host launcher prints* for the same scene
    # (`RoboTest.host.exe --desktop --no-window`: fills=12 labels=4 unshaped=0 windows=1 content_runs=1
    # content_pixels=652912), which is the point of one scene with two backends: a guest number that drifted
    # from the host's would mean the two launchers had stopped drawing the same desktop. `unshaped` is 0 or
    # the text in this checkpoint did not reach the picture.
    "ROBO_QEMU_DESKTOP_WINDOWS=0x00000001",
    "ROBO_QEMU_DESKTOP_CONTENT_RUNS=0x00000001",
    "ROBO_QEMU_DESKTOP_LABELS=0x00000004",
    "ROBO_QEMU_DESKTOP_UNSHAPED=0x00000000",
    "ROBO_QEMU_DESKTOP_FILLS=0x0000000C",
    "ROBO_QEMU_DESKTOP_PIXELS=0x0009F670",
    # And it reached the screen: every pixel of the 1280x800 destination was written.
    "ROBO_QEMU_PRESENT_OK",
    "ROBO_QEMU_PRESENT_COMPLETE=0x00000001",
    "ROBO_QEMU_PRESENT_PIXELS=0x000FA000",
    # Input travelled the whole way back: an injected press reached the shell as an action.
    "ROBO_QEMU_INPUT_OK",
    "ROBO_QEMU_DESKTOP_LIVE"
)

function Assert-RoboQemuAvailable {
    if (-not (Get-RoboTool "qemu")) {
        throw "qemu-system-x86_64 is not installed; run 'scripts/robo check'"
    }
    if (-not (Test-Path -LiteralPath $script:RoboQemu.FirmwareCode)) {
        throw "UEFI firmware missing: $($script:RoboQemu.FirmwareCode)"
    }
}

function Invoke-RoboQemuSerialRun {
    <#
      Boots the ESP image headlessly and waits for markers.

      Firmware/host combinations differ in which disk attachment they will boot
      from, so a second attachment is tried - but only when the first attempt
      produced no OS output at all. A run that booted and then failed its own
      selftests is a real failure and is never retried: retrying it would hide
      the failure and make the result depend on which attempt happened to pass.

      Each attempt gets its own serial log so a later attempt cannot overwrite
      the evidence from an earlier one.
    #>
    param(
        [Parameter(Mandatory)][string[]]$Markers,
        [string[]]$FailPatterns = @(),
        [int]$TimeoutSeconds = 180,
        [string]$Label = "QEMU"
    )
    Assert-RoboQemuAvailable

    $attempts = @($script:RoboQemu.DiskMode)
    if ($script:RoboQemu.DiskMode -ne "virtio") { $attempts += "virtio" }
    if ($script:RoboQemu.DiskMode -ne "ide") { $attempts += "ide" }

    $safeLabel = ($Label -replace "[^a-zA-Z0-9_]", "_")
    $last = $null
    foreach ($disk in $attempts) {
        $script:RoboQemu.DiskMode = $disk
        Write-RoboInfo "boot attempt: disk=$disk accel=$($script:RoboQemu.Accel)"
        Reset-RoboSerialLog
        $cmdArgs = Get-RoboQemuArgs -Headless
        $proc = Start-RoboQemu -Arguments $cmdArgs -Headless
        try {
            $last = Wait-RoboSerialMarkers -Markers $Markers -FailPatterns $FailPatterns `
                -TimeoutSeconds $TimeoutSeconds -Process $proc
        } finally {
            Stop-RoboQemu $proc
        }

        # Keep this attempt's evidence under its own name.
        $attemptLog = Join-Path $script:RoboOut.Logs ("qemu-$safeLabel-$disk.log")
        if ($last.Serial) { Write-RoboUtf8File -Path $attemptLog -Text $last.Serial }
        $last["Log"] = $attemptLog

        if ($last.Complete -or $last.Failed) { break }

        # Did the OS start at all? If yes, this is a genuine failure, not a
        # "firmware could not boot this disk" problem.
        $booted = @($Markers | Where-Object { $last.Serial -match $_ }).Count -gt 0
        if ($booted) {
            Write-RoboInfo "attempt produced OS output; not retrying"
            break
        }
        Write-RoboInfo "attempt produced no OS output; trying the next disk attachment"
    }
    return $last
}

function Assert-RoboQemuResult {
    <#
      Turns a marker wait into a verdict. On failure it quotes what the OS
      itself reported - any `[FAIL] ...` lines - so the message says which
      selftest failed rather than only that a marker never arrived.

      A marker that never arrived is reported together with every `[FAIL]` the
      OS printed, so "missing marker" always comes with the reason.
    #>
    param(
        [Parameter(Mandatory)][hashtable]$Result,
        [Parameter(Mandatory)][string]$Label,
        [Parameter(Mandatory)][string[]]$Markers
    )
    $log = $(if ($Result.Log) { $Result.Log } else { Join-Path $script:RoboOut.Logs "qemu-serial.log" })

    # The OS reports its own failures; surface them verbatim.
    $reported = @()
    foreach ($line in ($Result.Serial -split "`r?`n")) {
        if ($line -match '^\s*\[(FAIL|[A-Z_]*FAIL)\]') { $reported += $line.Trim() }
    }
    $reportedText = ""
    if ($reported.Count -gt 0) {
        $reportedText = "`nthe OS reported failures:" + "`n  " + (($reported | Select-Object -Unique) -join "`n  ")
    }

    if ($Result.Complete) {
        Write-RoboInfo ("serial: $log")
        return
    }
    if ($Result.Failed) {
        throw "$Label reported failure (matched '$($Result.FailedBy)')$reportedText`nserial: $log"
    }
    if ($Result.Missing.Count -gt 0) {
        throw ("$Label did not reach all markers after $($Result.Seconds)s.$reportedText`nmissing:" + "`n  " +
               ($Result.Missing -join "`n  ") + "`nserial: $log")
    }
    throw "$Label produced no usable serial output (serial: $log)"
}

function Invoke-RoboQemuBootTest {
    param([switch]$Continue)
    Invoke-RoboStep -Name "QEMU: bootloader selftest" -Layer "TEST" -Continue:$Continue -Action {
        Invoke-RoboBuildImageCommand -Flags "selftest`nmenu`nsplash_ms 0"
        $echo = Start-RoboNetEcho
        try {
            $res = Invoke-RoboQemuSerialRun -Markers @("ROBOBOOT_QEMU_OK") `
                -FailPatterns @("ROBOBOOT_QEMU_FAIL") -TimeoutSeconds $script:RoboTimeout.Bootloader `
                -Label "bootloader-selftest"
            Assert-RoboQemuResult -Result $res -Label "bootloader selftest" -Markers @("ROBOBOOT_QEMU_OK")
        } finally { Stop-RoboNetEcho $echo }
    } | Out-Null
}

function Invoke-RoboQemuKernelTest {
    param([switch]$Continue)
    Invoke-RoboStep -Name "QEMU: kernel + OS selftest" -Layer "TEST" -Continue:$Continue -Action {
        Invoke-RoboBuildImageCommand -Flags "ktest`nsplash_ms 0"
        $echo = Start-RoboNetEcho
        try {
            $res = Invoke-RoboQemuSerialRun -Markers $script:RoboKernelBootMarkers `
                -FailPatterns $script:RoboKernelFailPatterns `
                -TimeoutSeconds $script:RoboTimeout.Kernel -Label "kernel-selftest"
            Assert-RoboQemuResult -Result $res -Label "kernel selftest" -Markers $script:RoboKernelBootMarkers
        } finally { Stop-RoboNetEcho $echo }
    } | Out-Null
}

function Invoke-RoboQemuUserSpaceTest {
    <#
      The guest's own user-space selftest services. Kept separate from the kernel
      gate so a failure here is attributed to the user-space component that
      failed, not to the kernel.
    #>
    param([switch]$Continue)
    Invoke-RoboStep -Name "QEMU: user-space selftests" -Layer "TEST" -Continue:$Continue -Action {
        Invoke-RoboBuildImageCommand -Flags "ktest`nsplash_ms 0"
        $echo = Start-RoboNetEcho
        try {
            $res = Invoke-RoboQemuSerialRun -Markers $script:RoboUserSpaceMarkers `
                -FailPatterns $script:RoboUserSpaceFailPatterns `
                -TimeoutSeconds $script:RoboTimeout.Kernel -Label "userspace-selftest"
            Assert-RoboQemuResult -Result $res -Label "user-space selftest" -Markers $script:RoboUserSpaceMarkers
        } finally { Stop-RoboNetEcho $echo }
    } | Out-Null
}

function Invoke-RoboQemuDisplayTest {
    param([switch]$Continue)
    Invoke-RoboStep -Name "QEMU: the desktop shell on the display" -Layer "TEST" -Continue:$Continue -Action {
        Invoke-RoboBuildImageCommand -Flags "splash_ms 0"
        $echo = Start-RoboNetEcho
        try {
            $res = Invoke-RoboQemuSerialRun -Markers $script:RoboDisplayMarkers `
                -FailPatterns @("ROBO_QEMU_DISPLAY_FAIL") -TimeoutSeconds $script:RoboTimeout.Runtime `
                -Label "qemu-display"
            Assert-RoboQemuResult -Result $res -Label "QEMU display smoke" -Markers $script:RoboDisplayMarkers
        } finally { Stop-RoboNetEcho $echo }
    } | Out-Null
}

# --------------------------------------------------------------------------
# test-all
# --------------------------------------------------------------------------

function Invoke-RoboTestAll {
    <#
      Runs every real test surface. QEMU acceptance is skipped (and reported as
      skipped, never passed) when QEMU or the firmware is unavailable.
    #>
    Write-RoboSection "RoboticelaOS tests"
    $qemuAvailable = (Test-Path -LiteralPath $script:RoboQemu.FirmwareCode) -and (Get-RoboTool "qemu")
    if (-not $qemuAvailable -and -not $script:RoboFlag.SkipQemu) {
        $script:RoboFlag.SkipQemu = $true
        Write-RoboWarn "QEMU or UEFI firmware unavailable - QEMU acceptance will be skipped"
    }

    # --- 1. runtime host tests -------------------------------------------
    Build-RoboRuntimeForTests -Continue
    $coverage = Invoke-RoboRuntimeDiscoverCoverage
    if ($coverage.Total -eq 0) {
        Add-RoboResult -Name "runtime test discovery" -Status fail -Layer "TEST" `
            -Detail "the runtime test binary reported no suites"
        Write-RoboFail "runtime test binary reported no suites"
    } else {
        Write-RoboInfo ("runtime: " + $coverage.Total + " cases in " + $coverage.Groups.Count + " namespaces")
        foreach ($component in (Get-RoboRuntimeComponentNames)) {
            Invoke-RoboRuntimeComponentTest -Component $component -Continue
        }
        if ($coverage.Unclaimed.Count -gt 0) {
            foreach ($u in $coverage.Unclaimed) {
                Invoke-RoboRuntimeTestFilter -Name ("runtime: " + $u.Prefix.TrimEnd(".")) -Filter $u.Prefix -Continue
            }
        }
    }

    # --- 2. bare-metal host tests ----------------------------------------
    Invoke-RoboBareHostTests -Continue

    # --- 3. QEMU acceptance ----------------------------------------------
    if ($script:RoboFlag.SkipQemu) {
        foreach ($n in @("QEMU: bootloader selftest", "QEMU: kernel + OS selftest",
                        "QEMU: user-space selftests", "QEMU: the desktop shell on the display")) {
            Add-RoboResult -Name $n -Status skip -Layer "TEST" -Detail "QEMU unavailable or --skip-qemu"
            Write-RoboSkipped $n
        }
    } else {
        Invoke-RoboQemuBootTest -Continue
        Invoke-RoboQemuKernelTest -Continue
        Invoke-RoboQemuUserSpaceTest -Continue
        Invoke-RoboQemuDisplayTest -Continue
    }

    return (-not $script:RoboFailed)
}

# --------------------------------------------------------------------------
# Component test commands
# --------------------------------------------------------------------------

function Invoke-RoboTestComponent {
    param(
        [Parameter(Mandatory)][string]$Component,
        [switch]$Continue
    )
    if ($Component -eq "bootloader" -or $Component -eq "kernel" -or $Component -eq "libc") {
        Build-RoboRuntimeForTests -Continue:$Continue
        Invoke-RoboBareHostTests -Continue:$Continue
        return
    }
    if ($Component -in @("qemu-boot", "qemu-kernel", "qemu-userspace", "qemu-display")) {
        if ($script:RoboFlag.SkipQemu) {
            Write-RoboSkipped ("$Component (QEMU unavailable or --skip-qemu)")
            Add-RoboResult -Name $Component -Status skip -Layer "TEST" -Detail "QEMU unavailable"
            return
        }
        switch ($Component) {
            "qemu-boot"       { Invoke-RoboQemuBootTest -Continue:$Continue }
            "qemu-kernel"     { Invoke-RoboQemuKernelTest -Continue:$Continue }
            "qemu-userspace"  { Invoke-RoboQemuUserSpaceTest -Continue:$Continue }
            "qemu-display"    { Invoke-RoboQemuDisplayTest -Continue:$Continue }
        }
        return
    }
    Build-RoboRuntimeForTests -Continue:$Continue -BuildOnly
    Invoke-RoboRuntimeComponentTest -Component $Component -Continue:$Continue
}

function Invoke-RoboTestE2E {
    <#
      The real flow: build -> boot -> OS selftests -> the desktop shell ->
      deployment validation. This is deliberately the strictest command: it
      performs a full build, so it cannot pass on stale artifacts.
    #>
    Write-RoboSection "End-to-end: source -> build -> boot -> desktop -> shutdown"

    Invoke-RoboStep -Name "environment" -Layer "E2E" -Action {
        if (-not (Invoke-RoboCheck)) { throw "environment check failed" }
    } | Out-Null

    $script:RoboFlag.Rebuild = $true
    Invoke-RoboBuildAll

    # Every stage runs even when an earlier one failed, so one invocation
    # reports the whole chain instead of stopping at the first fault. The
    # command still exits non-zero - a continued failure is recorded, not
    # excused.
    $script:RoboFlag.SkipQemu = $false
    Invoke-RoboQemuKernelTest -Continue
    Invoke-RoboQemuUserSpaceTest -Continue
    Invoke-RoboQemuDisplayTest -Continue

    Invoke-RoboStep -Name "deployment artifact" -Layer "E2E" -Continue -Action {
        if (-not (Invoke-RoboValidateDeployment)) {
            throw "deployment validation failed; see the report above"
        }
    } | Out-Null

    if ($script:RoboFailed) {
        $failed = @($script:RoboResults.Suites |
            Where-Object { $_.status -eq "fail" } | ForEach-Object { $_.name })
        Write-RoboFail ("end-to-end chain failed at: " + ($failed -join ", "))
        return $false
    }
    Write-RoboPass "end-to-end chain complete"
    return $true
}
