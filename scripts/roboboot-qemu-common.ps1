# Shared helpers for RoboBoot QEMU run/test scripts.

. (Join-Path $PSScriptRoot "roboboot-build-common.ps1")

function Get-Qemu {
    $q = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
    if ($q) { return $q.Source }
    $hint = Join-Path ${env:ProgramFiles} "qemu\qemu-system-x86_64.exe"
    if (Test-Path $hint) { return $hint }
    throw "qemu-system-x86_64 not found"
}

function Get-OvmfCode {
    $code = Join-Path ${env:ProgramFiles} "qemu\share\edk2-x86_64-code.fd"
    if (Test-Path $code) { return $code }
    throw "EDK2 firmware missing: $code"
}

function Get-OvmfVarsTemplate {
    $vars = Join-Path ${env:ProgramFiles} "qemu\share\edk2-i386-vars.fd"
    if (Test-Path $vars) { return $vars }
    throw "EDK2 vars template missing: $vars"
}

function Get-QemuArgLine {
    param([string[]]$Arguments)
    ($Arguments | ForEach-Object {
        $a = "$_"
        if ($a -match '[\s"]') { '"' + ($a -replace '"', '\"') + '"' } else { $a }
    }) -join ' '
}

function New-OvmfVars {
    Copy-Item (Get-OvmfVarsTemplate) $script:VarsFd -Force
}

function Get-QemuBaseArgs {
    param(
        [switch]$Headless,
        [string]$Disk = "ide"
    )
    $code = Get-OvmfCode
    New-OvmfVars
    $args = @(
        "-machine", "q35",
        "-cpu", "qemu64",
        "-m", "512",
        "-vga", "std",
        "-net", "none",
        "-boot", "order=c,menu=off",
        "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=$code",
        "-drive", "if=pflash,format=raw,unit=1,file=$script:VarsFd",
        "-serial", "file:$script:SerialLog",
        "-debugcon", "file:$script:OvmfDebug",
        "-global", "isa-debugcon.iobase=0x402"
    )
    if ($Disk -eq "virtio") {
        $args += @(
            "-drive", "file=$script:EspImg,format=raw,if=none,id=esp",
            "-device", "virtio-blk-pci,drive=esp,bootindex=1"
        )
    } else {
        $args += @("-hda", $script:EspImg)
    }
    if ($Headless) {
        $args += @("-display", "none", "-no-reboot")
    } else {
        # Hide the GTK menu so Alt+M reaches the firmware instead of QEMU's Machine menu.
        $args += @("-display", "gtk,show-menubar=off,grab-on-hover=on")
    }
    return $args
}

function Start-QemuProcess {
    param(
        [string[]]$Arguments,
        [string]$Accel = "tcg",
        [switch]$Hidden
    )
    $qemu = Get-Qemu
    $all = @("-accel", $Accel) + $Arguments
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $qemu
    $psi.Arguments = Get-QemuArgLine $all
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = [bool]$Hidden
    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $psi
    if (-not $p.Start()) { throw "failed to start QEMU" }
    return $p
}
