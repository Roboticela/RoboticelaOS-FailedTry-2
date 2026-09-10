# Build RoboBoot as a UEFI app and boot it in QEMU (EDK2/OVMF).
#   .\scripts\run-bootloader-qemu.ps1
#   .\scripts\run-bootloader-qemu.ps1 -Menu
# Press Alt+M during splash for the startup picker. Click the VM window first.

param(
    [switch]$Menu,
    [switch]$HoldAlt,
    [switch]$SkipBuild,
    [switch]$Headless,
    [switch]$BuildOnly,
    [string]$Accel = "",
    [string]$Disk = "ide"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "roboboot-qemu-common.ps1")

Push-Location $script:Boot
try {
    if (-not $SkipBuild) { Invoke-BuildEfi }
    $flagParts = @()
    if ($Menu -or $HoldAlt) { $flagParts += "menu" }
    Invoke-MakeEsp -Flags ($flagParts -join "`n")
    if ($BuildOnly) {
        Write-Host "Built $script:EfiOut"
        Write-Host "ESP    $script:EspImg"
        exit 0
    }

    if (Test-Path $script:SerialLog) { Remove-Item $script:SerialLog -Force }

    $qemu = Get-Qemu
    $qemuArgs = Get-QemuBaseArgs -Headless:$Headless -Disk $Disk
    Write-Host "RoboBoot QEMU"
    Write-Host "  Firmware: $(Get-OvmfCode)"
    Write-Host "  Disk:     $script:EspImg"
    Write-Host "  Press Alt+M during splash for the startup picker (or pass -Menu)."

    if ($Accel) {
        & $qemu @qemuArgs -accel $Accel
        exit $LASTEXITCODE
    }

    & $qemu @qemuArgs -accel "whpx,kernel-irqchip=off"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "WHPX unavailable, using TCG."
        & $qemu @qemuArgs -accel tcg
    }
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
