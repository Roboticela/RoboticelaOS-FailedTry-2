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
    param([switch]$Reset)
    if ($Reset -or -not (Test-Path $script:VarsFd)) {
        Copy-Item (Get-OvmfVarsTemplate) $script:VarsFd -Force
    }
}

function Get-QemuBaseArgs {
    param(
        [switch]$Headless,
        [string]$Disk = "ide",
        [switch]$ResetNvram,
        [switch]$FirmwareMenu
    )
    $code = Get-OvmfCode
    New-OvmfVars -Reset:($ResetNvram -or $Headless)
    $boot = if ($FirmwareMenu) { "order=c,menu=on,splash-time=8000" } else { "order=c,menu=off,splash-time=0" }
    $args = @(
        "-machine", "q35,usb=off",
        "-cpu", "qemu64,-svm",
        "-m", "512",
        "-vga", "std",
        "-netdev", "user,id=n0,net=10.0.2.0/24,dhcpstart=10.0.2.15,guestfwd=tcp:10.0.2.4:18019-tcp:127.0.0.1:18019,guestfwd=tcp:10.0.2.4:18080-tcp:127.0.0.1:18080",
        "-device", "virtio-net-pci,netdev=n0,disable-legacy=on",
        "-boot", $boot,
        "-drive", "if=pflash,format=raw,unit=0,readonly=on,file=$code",
        "-drive", "if=pflash,format=raw,unit=1,file=$script:VarsFd",
        "-fw_cfg", "name=opt/org.tianocore/EFIShellSupport,string=no",
        "-serial", "file:$script:SerialLog"
    )
    if (-not $FirmwareMenu) {
        $args += @("-fw_cfg", "name=opt/org.tianocore/FirmwareSetupSupport,string=no")
    }
    if ($Disk -eq "virtio") {
        $args += @(
            "-drive", "file=$script:EspImg,format=raw,if=none,id=esp,cache=writethrough",
            "-device", "virtio-blk-pci,drive=esp,bootindex=1"
        )
    } else {
        $args += @("-drive", "file=$script:EspImg,format=raw,if=ide,index=0,media=disk,cache=writethrough")
    }
    $vblk = Join-Path $script:Build "virtio-test.img"
    if (-not (Test-Path $vblk)) {
        $fs = [System.IO.File]::Open($vblk, 'Create', 'Write')
        $fs.SetLength(8MB)
        $fs.Close()
    }
    $args += @(
        "-drive", "file=$vblk,format=raw,if=none,id=vblk,cache=writethrough",
        "-device", "virtio-blk-pci,disable-legacy=on,drive=vblk"
    )
    if ($Headless) {
        $args += @("-display", "none", "-no-reboot")
    } else {
        # Hide the GTK menu so Alt+M reaches the firmware instead of QEMU's Machine menu.
        $args += @("-display", "gtk,show-menubar=off,grab-on-hover=on")
    }
    return $args
}

function Test-RoboLocalPort([int]$Port) {
    $c = $null
    try {
        $c = New-Object System.Net.Sockets.TcpClient
        $iar = $c.BeginConnect("127.0.0.1", $Port, $null, $null)
        if (-not $iar.AsyncWaitHandle.WaitOne(250)) { return $false }
        $c.EndConnect($iar)
        return $c.Connected
    } catch {
        return $false
    } finally {
        if ($c) { $c.Close() }
    }
}

function Clear-RoboSerialLog {
    if (-not (Test-Path $script:SerialLog)) { return }
    try {
        Remove-Item $script:SerialLog -Force -ErrorAction Stop
        return
    } catch {}
    Get-Process qemu-system-x86_64 -ErrorAction SilentlyContinue | ForEach-Object {
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 400
    Remove-Item $script:SerialLog -Force -ErrorAction SilentlyContinue
}

function Stop-RoboNetEchoLeftovers {
    Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Name -match '^python(w)?(\.exe)?$' -and
            $_.CommandLine -and
            $_.CommandLine -like '*net-echo-server.py*'
        } |
        ForEach-Object {
            Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
        }
}

function Start-RoboNetEcho {
    if ((Test-RoboLocalPort 18019) -and (Test-RoboLocalPort 18080)) {
        return $null
    }
    if ((Test-RoboLocalPort 18019) -or (Test-RoboLocalPort 18080)) {
        Stop-RoboNetEchoLeftovers
        Start-Sleep -Milliseconds 200
    }
    $py = Get-Python
    $echoPy = Join-Path $PSScriptRoot "net-echo-server.py"
    $proc = Start-Process -FilePath $py -ArgumentList @($echoPy) -PassThru -WindowStyle Hidden
    $deadline = (Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $deadline) {
        if ((Test-RoboLocalPort 18019) -and (Test-RoboLocalPort 18080)) {
            return $proc
        }
        if ($proc.HasExited) { break }
        Start-Sleep -Milliseconds 100
    }
    if ((Test-RoboLocalPort 18019) -and (Test-RoboLocalPort 18080)) {
        return $proc
    }
    if ($proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
    throw "host net-echo-server failed to listen on 127.0.0.1:18019 and :18080"
}

function Stop-RoboNetEcho($Proc) {
    if ($Proc -and -not $Proc.HasExited) {
        Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
    }
}

function Start-QemuProcess {
    param(
        [string[]]$Arguments,
        [string]$Accel = "tcg",
        [switch]$Hidden
    )
    $qemu = Get-Qemu
    $all = @("-accel", "$Accel,thread=multi") + $Arguments
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
