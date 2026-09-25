# RoboticelaOS pipeline — run commands.
#
# Each run command launches a real artifact:
#
#   run-bootloader      bootloader host preview window (no emulation)
#   run-kernel          QEMU, real UEFI boot of kernel.elf
#   run-runtime         host window, the desktop shell in a native Win32 window
#   run-runtime-qemu    QEMU, the same desktop shell inside the OS
#   run-qemu            full build of the whole OS, then boot it in QEMU
#
# -Headless switches the QEMU commands from a visible window to marker
# checking, which is what CI and remote sessions need.

function Assert-RoboHostWindowPossible {
    Assert-RoboWindowsHost
    if (-not $script:RoboFlag.Headless) {
        if ($env:SESSIONNAME -and $env:SESSIONNAME -notmatch "^(Console|RDP)") {
            throw ("no interactive desktop session detected ($($env:SESSIONNAME)); " +
                   "use -Headless or run from a desktop session")
        }
    }
}

function Invoke-RoboQemuVisible {
    <#
      Starts QEMU with a real display and waits for the user to close it.
      Networking loopback helpers are started/stopped around it so the OS's own
      network tests keep working while the window is up.
    #>
    param(
        [switch]$Headless,
        [int]$TimeoutSeconds = 0
    )
    Initialize-RoboEngine
    Assert-RoboQemuAvailable
    Assert-RoboFile $script:RoboEngineImage "disk image" | Out-Null

    Write-RoboKeyValue "Image" $script:RoboEngineImage
    Write-RoboKeyValue "Firmware" $script:RoboQemu.FirmwareCode
    Write-RoboKeyValue "Firmware vars" $script:VarsFd
    Write-RoboKeyValue "Memory" ("$($script:RoboQemu.MemoryMB) MB")
    Write-RoboKeyValue "Accel" $script:RoboQemu.Accel
    Write-RoboKeyValue "Display" $(if ($Headless) { "none (headless)" } else { $script:RoboQemu.Display })
    Write-RoboKeyValue "Serial log" $script:SerialLog

    $cmdArgs = Get-RoboQemuArgs -Headless:$Headless
    if ($script:RoboFlag.Verbose) { Write-RoboInfo ("qemu " + (Get-RoboArgLine $cmdArgs)) }

    $echo = $null
    $proc = $null
    try {
        $echo = Start-RoboNetEcho
        $proc = Start-RoboQemu -Arguments $cmdArgs -Headless:$Headless
        if (-not $Headless) {
            Write-RoboInfo "close the QEMU window to stop"
            if ($TimeoutSeconds -gt 0) {
                if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
                    Write-RoboWarn "still running after ${TimeoutSeconds}s; stopping"
                    Stop-RoboQemu $proc
                }
            } else {
                $proc.WaitForExit()
            }
            return $proc.ExitCode
        }

        Write-RoboInfo "headless: waiting for the OS to come up"
        $res = Wait-RoboSerialMarkers -Markers @("Roboticela Kernel", "Boot protocol: Roboticela") `
            -TimeoutSeconds $(if ($TimeoutSeconds -gt 0) { $TimeoutSeconds } else { $script:RoboTimeout.Kernel }) `
            -Process $proc
        $log = Join-Path $script:RoboOut.Logs "qemu-run-serial.log"
        if ($res.Serial) { Write-RoboUtf8File -Path $log -Text $res.Serial }
        if (-not $res.Complete) {
            throw ("the OS did not reach kernel startup in ${TimeoutSeconds}s; serial: $log")
        }
        Write-RoboInfo ("serial: $log")
        return 0
    } finally {
        Stop-RoboQemu $proc
        Stop-RoboNetEcho $echo
    }
}

function Invoke-RoboRunBootloader {
    if (-not $script:RoboFlag.SkipBuild) {
        Invoke-RoboStep -Name "bootloader host preview" -Layer "BUILD" -Action {
            Invoke-RoboEngineBootloaderHost
            Assert-RoboFile $script:HostOut "host preview binary" | Out-Null
        } | Out-Null
    }
    Assert-RoboHostWindowPossible
    Assert-RoboFile $script:HostOut "host preview binary" | Out-Null

    $cmdArgs = @("--root", $script:Boot,
        "--width", "$($script:RoboRun.Width)", "--height", "$($script:RoboRun.Height)")
    if ($script:RoboFlag.Menu) { $cmdArgs += "--menu" }
    if ($script:RoboFlag.HoldAlt) { $cmdArgs += "--hold-alt" }
    if ($script:RoboFlag.Visible) { $cmdArgs += "--fullscreen" }

    Write-RoboKeyValue "Preview" $script:HostOut
    Write-RoboInfo "the preview simulates splash, picker and kernel log; the real OS boots with 'scripts/robo run-qemu'"
    Write-RoboInfo "Alt+M opens the startup picker"
    $r = Invoke-RoboProcess -FilePath $script:HostOut -Arguments $cmdArgs `
        -WorkingDirectory $script:Boot -Echo
    return $r.ExitCode
}

function Invoke-RoboRunRuntime {
    <#
      The host visual vertical slice: the shared desktop scene (markup → RoboHTML →
      RoboCSS → RoboLayout → RoboPaint → RoboRender) composited by the shell into a
      real native window through the host display backend.

      This is the same scene `run-runtime-qemu` boots to: one definition of the
      checkpoint, two launchers, so what is on screen here is what the OS shows.
    #>
    Build-RoboRuntimeForTests
    Assert-RoboHostWindowPossible
    $exe = Get-RoboRuntimeTestBinary
    Assert-RoboFile $exe "runtime test binary (desktop shell launcher)" | Out-Null

    $cmdArgs = @("--desktop")
    if ($script:RoboFlag.Headless) { $cmdArgs += "--no-window" }
    elseif ($script:RoboRun.HoldMs -gt 0) { $cmdArgs += @("--hold", "$($script:RoboRun.HoldMs)") }
    if ($script:RoboRun.Shot) { $cmdArgs += @("--shot", $script:RoboRun.Shot) }

    Write-RoboKeyValue "Runtime" $exe
    Write-RoboKeyValue "Scene" "RoboDesktopScene (the desktop shell and one document window)"
    Write-RoboKeyValue "Mode" $(if ($script:RoboFlag.Headless) { "offscreen render + pixel probes" } else { "native window" })
    # A visible window wants live output; the offscreen run is captured so a
    # failed pixel probe can be diagnosed from the log.
    $r = Invoke-RoboProcess -FilePath $exe -Arguments $cmdArgs `
        -WorkingDirectory $script:RoboOut.Runtime -TimeoutSeconds $script:RoboTimeout.Runtime `
        -Echo:(-not $script:RoboFlag.Headless) -LogName "run-runtime"
    if ($r.TimedOut) { throw "the host runtime did not exit within $($script:RoboTimeout.Runtime)s" }
    if ($r.ExitCode -ne 0) {
        throw ("host runtime exited with code $($r.ExitCode)`n" + (Get-RoboTail $r.Output 40))
    }
    return 0
}

function Invoke-RoboRunKernel {
    param([switch]$Continue)
    Invoke-RoboStep -Name "build kernel + disk image" -Layer "BUILD" -Continue:$Continue -Action {
        Invoke-RoboBuildImageCommand -Flags "splash_ms 0"
    } | Out-Null
    if ($Continue -and $script:RoboFailed) { return 1 }
    return (Invoke-RoboQemuVisible -Headless:$script:RoboFlag.Headless)
}

function Invoke-RoboRunRuntimeQemu {
    param([switch]$Continue)
    Invoke-RoboStep -Name "build kernel + runtime + disk image" -Layer "BUILD" -Continue:$Continue -Action {
        Invoke-RoboBuildImageCommand -Flags "splash_ms 0"
    } | Out-Null
    if ($Continue -and $script:RoboFailed) { return 1 }
    Write-RoboInfo "the OS boots to the desktop shell; its window is a real HTML document in the QEMU display"
    return (Invoke-RoboQemuVisible -Headless:$script:RoboFlag.Headless)
}

function Invoke-RoboRunQemu {
    <#
      Build (incremental unless --rebuild), then boot. Object-level resume and
      Build/.build-all.json stage resume keep a second run from redoing finished work.
    #>
    Invoke-RoboBuildAll -EspFlags "splash_ms 0"
    Write-RoboBlank
    return (Invoke-RoboQemuVisible -Headless:$script:RoboFlag.Headless)
}
