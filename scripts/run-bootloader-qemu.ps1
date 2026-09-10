# Build RoboBoot as a UEFI app and boot it in QEMU (EDK2/OVMF).
#   .\scripts\run-bootloader-qemu.ps1
#   .\scripts\run-bootloader-qemu.ps1 -Menu
#   .\scripts\run-bootloader-qemu.ps1 -FirmwareMenu
# Press Alt+M during splash for the startup picker. Click the VM window first.
# -FirmwareMenu pauses firmware so ESC/F12 can show the RoboticelaOS boot entry
# (run once to install it, then again without -ResetNvram to see the name).

param(
    [switch]$Menu,
    [switch]$HoldAlt,
    [switch]$SkipBuild,
    [switch]$Headless,
    [switch]$BuildOnly,
    [switch]$ResetNvram,
    [switch]$FirmwareMenu,
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
    $qemuArgs = Get-QemuBaseArgs -Headless:$Headless -Disk $Disk -ResetNvram:$ResetNvram -FirmwareMenu:$FirmwareMenu
    Write-Host "RoboBoot QEMU"
    Write-Host "  Firmware: $(Get-OvmfCode)"
    Write-Host "  Disk:     $script:EspImg"
    Write-Host "  Close the QEMU window to quit. Press Alt+M during splash for the startup picker (or pass -Menu)."
    if ($FirmwareMenu) {
        Write-Host "  Firmware boot menu: press ESC (or F12) during the firmware pause to see RoboticelaOS."
        Write-Host "  First boot installs the name; quit and run again (without -ResetNvram) to list it."
    }

    if ($Accel) {
        Write-Host "  Accel:    $Accel"
        & $qemu @qemuArgs -accel $Accel
        exit $LASTEXITCODE
    }

    # WHPX + std VGA GOP often aborts QEMU on Windows (exit 0, no window).
    Write-Host "  Accel:    tcg"
    & $qemu @qemuArgs -accel tcg
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
