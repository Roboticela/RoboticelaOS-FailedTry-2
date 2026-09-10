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
    if (Test-Path $script:SerialLog) { Remove-Item $script:SerialLog -Force }
    if (Test-Path $script:OvmfDebug) { Remove-Item $script:OvmfDebug -Force }

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
try {
    if (-not $SkipBuild) { Invoke-BuildEfi }
    Invoke-MakeEsp -Flags "selftest`nmenu`nsplash_ms 0"

    $result = Invoke-QemuSelftest -DiskKind $Disk
    if (-not $result.Ok -and -not $result.Fail -and $Disk -eq "ide") {
        Write-Host "IDE boot produced no RoboBoot marker; retrying virtio-blk."
        $result = Invoke-QemuSelftest -DiskKind "virtio"
    }

    $serial = $result.Serial
    if ($result.Ok) {
        Write-Host "RoboBoot QEMU tests passed"
        if ($serial) { Write-Host $serial }
        exit 0
    }
    Write-Host "RoboBoot QEMU tests failed"
    if ($serial) { Write-Host $serial } else { Write-Host "(no serial output - firmware may not have reached RoboBoot)" }
    if (Test-Path $script:OvmfDebug) {
        Write-Host "OVMF debugcon (tail):"
        Get-Content $script:OvmfDebug -Tail 40
    }
    exit 1
}
finally {
    Pop-Location
}
