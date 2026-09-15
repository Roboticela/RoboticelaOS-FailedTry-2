# Headless QEMU display smoke (Goal 7.12).
#   .\scripts\test-qemu-display.ps1
#   .\scripts\test-qemu-display.ps1 -SkipBuild

param(
    [switch]$SkipBuild,
    [int]$TimeoutSec = 240,
    [string]$Disk = "ide"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "roboboot-qemu-common.ps1")

function Read-Serial {
    if (-not (Test-Path $script:SerialLog)) { return "" }
    try {
        $fs = [System.IO.File]::Open(
            $script:SerialLog,
            [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read,
            [System.IO.FileShare]::ReadWrite)
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

function Test-QemuDisplayMarkers([string]$Text) {
    return (
        $Text -match "ROBO_QEMU_DISPLAY_OK" -and
        $Text -match "ROBO_QEMU_PRESENT_OK" -and
        $Text -match "ROBO_QEMU_INPUT_OK" -and
        $Text -match "ROBO_QEMU_DISPLAY_SHUTDOWN_OK"
    )
}

Push-Location $script:Boot
$echoProc = $null
try {
    if (-not $SkipBuild) {
        Invoke-RoboCombine -Flags "splash_ms 0"
    } else {
        Invoke-MakeEsp -Flags "splash_ms 0"
    }

    $py = Get-Python
    $echoPy = Join-Path $PSScriptRoot "net-echo-server.py"
    if (Test-Path $echoPy) {
        $echoProc = Start-Process -FilePath $py -ArgumentList @($echoPy) -PassThru -WindowStyle Hidden
        Start-Sleep -Milliseconds 400
    }

    Clear-RoboSerialLog
    $qemuArgs = Get-QemuBaseArgs -Headless -Disk $Disk
    $proc = Start-QemuProcess -Arguments $qemuArgs -Accel tcg -Hidden
    $serial = ""
    try {
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        while ((Get-Date) -lt $deadline) {
            $serial = Read-Serial
            if (Test-QemuDisplayMarkers $serial) { break }
            if ($serial -match "ROBO_QEMU_DISPLAY_FAIL") { break }
            if ($proc.HasExited) { break }
            Start-Sleep -Milliseconds 400
        }
    } finally {
        if ($proc -and -not $proc.HasExited) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
    }

    if (-not (Test-QemuDisplayMarkers $serial) -and $Disk -eq "ide") {
        Write-Host "IDE boot missed QEMU display markers; retrying virtio-blk."
        Clear-RoboSerialLog
        $qemuArgs = Get-QemuBaseArgs -Headless -Disk virtio
        $proc = Start-QemuProcess -Arguments $qemuArgs -Accel tcg -Hidden
        try {
            $deadline = (Get-Date).AddSeconds($TimeoutSec)
            while ((Get-Date) -lt $deadline) {
                $serial = Read-Serial
                if (Test-QemuDisplayMarkers $serial) { break }
                if ($serial -match "ROBO_QEMU_DISPLAY_FAIL") { break }
                if ($proc.HasExited) { break }
                Start-Sleep -Milliseconds 400
            }
        } finally {
            if ($proc -and -not $proc.HasExited) {
                Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            }
        }
    }

    if (Test-QemuDisplayMarkers $serial) {
        Write-Host "QEMU display smoke PASSED (Goal 7.12)"
        Write-Host $serial
        exit 0
    }

    Write-Host "QEMU display smoke FAILED"
    if ($serial) { Write-Host $serial } else { Write-Host "(no serial output)" }
    if (Test-Path $script:OvmfDebug) {
        Write-Host "OVMF debugcon (tail):"
        Get-Content $script:OvmfDebug -Tail 40
    }
    exit 1
}
finally {
    if ($echoProc -and -not $echoProc.HasExited) {
        Stop-Process -Id $echoProc.Id -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}
