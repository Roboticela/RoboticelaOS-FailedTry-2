# Headless QEMU boot test for RoboBoot (UEFI).
#   .\scripts\test-bootloader-qemu.ps1

param(
    [switch]$SkipBuild,
    [int]$TimeoutSec = 90,
    [string]$Disk = "ide"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "roboboot-qemu-common.ps1")

function Read-Serial {
    if (-not (Test-Path $script:SerialLog)) { return "" }
    try {
        $fs = [System.IO.File]::Open($script:SerialLog, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        try {
            $sr = New-Object System.IO.StreamReader($fs)
            return $sr.ReadToEnd()
        } finally {
            $fs.Dispose()
        }
    } catch {
        return ""
    }
}

function Invoke-QemuSelftest {
    param([string]$DiskKind)
    if (Test-Path $script:SerialLog) { Remove-Item $script:SerialLog -Force -ErrorAction SilentlyContinue }
    if (Test-Path $script:OvmfDebug) { Remove-Item $script:OvmfDebug -Force -ErrorAction SilentlyContinue }

    $qemuArgs = Get-QemuBaseArgs -Headless -Disk $DiskKind
    $proc = Start-QemuProcess -Arguments $qemuArgs -Accel tcg -Hidden
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        while ((Get-Date) -lt $deadline) {
            $text = Read-Serial
            if ($text -match "ROBOBOOT_QEMU_OK") { return @{ Ok = $true; Fail = $false; Serial = $text } }
            if ($text -match "ROBOBOOT_QEMU_FAIL") { return @{ Ok = $false; Fail = $true; Serial = $text } }
            if ($proc.HasExited) { break }
            Start-Sleep -Milliseconds 250
        }
        return @{ Ok = $false; Fail = $false; Serial = (Read-Serial) }
    } finally {
        if ($proc -and -not $proc.HasExited) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

Push-Location $script:Boot
$echoProc = $null
try {
    if (-not $SkipBuild) { Invoke-RoboCombine -Flags "selftest`nmenu`nsplash_ms 0" }
    else { Invoke-MakeEsp -Flags "selftest`nmenu`nsplash_ms 0" }

    $py = Get-Python
    $echoPy = Join-Path $PSScriptRoot "net-echo-server.py"
    $echoProc = Start-Process -FilePath $py -ArgumentList @($echoPy) -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 400

    $result = Invoke-QemuSelftest -DiskKind $Disk
    if (-not $result.Ok -and -not $result.Fail -and $Disk -eq "ide") {
        Write-Host "IDE boot produced no RoboBoot marker; retrying virtio-blk."
        $result = Invoke-QemuSelftest -DiskKind "virtio"
    }

    $serial = $result.Serial
    if (-not $result.Ok) {
        Write-Host "RoboBoot QEMU tests failed"
        if ($serial) { Write-Host $serial } else { Write-Host "(no serial output - firmware may not have reached RoboBoot)" }
        if ($result.Fail) {
            Write-Host "selftest reported ROBOBOOT_QEMU_FAIL"
        }
        if (Test-Path $script:OvmfDebug) {
            Write-Host "OVMF debugcon (tail):"
            Get-Content $script:OvmfDebug -Tail 40
        }
        exit 1
    }

    Write-Host "RoboBoot QEMU selftest passed"
    Invoke-MakeEsp -Flags "ktest`nsplash_ms 0"
    if (Test-Path $script:SerialLog) { Remove-Item $script:SerialLog -Force -ErrorAction SilentlyContinue }
    $kargs = Get-QemuBaseArgs -Headless -Disk $Disk
    $kproc = Start-QemuProcess -Arguments $kargs -Accel tcg -Hidden
    $kserial = ""
    try {
        $deadline = (Get-Date).AddSeconds([Math]::Max($TimeoutSec, 300))
        while ((Get-Date) -lt $deadline) {
            $kserial = Read-Serial
            if ($kserial -match "Roboticela Kernel" -and $kserial -match "Boot protocol: Roboticela" -and $kserial -match "CPU_TEST_OK" -and $kserial -match "PMM_TEST_OK" -and $kserial -match "VMM_TEST_OK" -and $kserial -match "HEAP_TEST_OK" -and $kserial -match "PF_TEST_OK" -and $kserial -match "TIMER_TEST_OK" -and $kserial -match "CTX_TEST_OK" -and $kserial -match "SCHED_TEST_OK" -and $kserial -match "THREAD_TEST_OK" -and $kserial -match "PROC_TEST_OK" -and $kserial -match "KERNEL PANIC" -and $kserial -match "Page Fault" -and $kserial -match "Present : NO" -and $kserial -match "Access  : READ" -and $kserial -match "Mode    : KERNEL" -and $kserial -match "Hello from user space." -and $kserial -match "\[PASS\] ROBOTICELA OS PHASE 10" -and $kserial -match "\[PASS\] ROBOTICELA OS PHASE 11" -and $kserial -match "\[PASS\] ROBOTICELA OS FILE DESCRIPTORS" -and $kserial -match "\[PASS\] ROBOTICELA OS VFS" -and $kserial -match "\[PASS\] ROBOFS v1" -and $kserial -match "Hello from RoboFS." -and $kserial -match "USER_ROBOFS_OK" -and $kserial -match "\[PASS\] DRIVER SYSTEM" -and $kserial -match "\[PASS\] STORAGE DRIVER" -and $kserial -match "\[PASS\] Window Server" -and $kserial -match "\[PASS\] RoboCompositor" -and $kserial -match "\[PASS\] INIT PROCESS" -and $kserial -match "\[PASS\] NETWORK" -and $kserial -match "\[PASS\] LIBC") { break }
            if ($kproc.HasExited) { break }
            Start-Sleep -Milliseconds 250
        }
    } finally {
        if ($kproc -and -not $kproc.HasExited) {
            Stop-Process -Id $kproc.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($kserial -match "Roboticela Kernel" -and $kserial -match "Boot protocol: Roboticela" -and $kserial -match "CPU_TEST_OK" -and $kserial -match "PMM_TEST_OK" -and $kserial -match "VMM_TEST_OK" -and $kserial -match "HEAP_TEST_OK" -and $kserial -match "PF_TEST_OK" -and $kserial -match "TIMER_TEST_OK" -and $kserial -match "CTX_TEST_OK" -and $kserial -match "SCHED_TEST_OK" -and $kserial -match "THREAD_TEST_OK" -and $kserial -match "PROC_TEST_OK" -and $kserial -match "KERNEL PANIC" -and $kserial -match "Page Fault" -and $kserial -match "Present : NO" -and $kserial -match "Access  : READ" -and $kserial -match "Mode    : KERNEL" -and $kserial -match "Hello from user space." -and $kserial -match "\[PASS\] ROBOTICELA OS PHASE 10" -and $kserial -match "\[PASS\] ROBOTICELA OS PHASE 11" -and $kserial -match "\[PASS\] ROBOTICELA OS FILE DESCRIPTORS" -and $kserial -match "\[PASS\] ROBOTICELA OS VFS" -and $kserial -match "\[PASS\] ROBOFS v1" -and $kserial -match "Hello from RoboFS." -and $kserial -match "USER_ROBOFS_OK" -and $kserial -match "\[PASS\] DRIVER SYSTEM" -and $kserial -match "\[PASS\] STORAGE DRIVER" -and $kserial -match "\[PASS\] Window Server" -and $kserial -match "\[PASS\] RoboCompositor" -and $kserial -match "\[PASS\] INIT PROCESS" -and $kserial -match "\[PASS\] NETWORK" -and $kserial -match "\[PASS\] LIBC") {
        Write-Host "RoboBoot kernel QEMU tests passed"
        Write-Host $kserial
        exit 0
    }
    Write-Host "RoboBoot kernel QEMU tests failed"
    if ($kserial) { Write-Host $kserial } else { Write-Host "(no kernel serial output)" }
    exit 1
}
finally {
    if ($echoProc -and -not $echoProc.HasExited) {
        Stop-Process -Id $echoProc.Id -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}
