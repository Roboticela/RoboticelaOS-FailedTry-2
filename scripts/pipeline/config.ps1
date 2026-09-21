# RoboticelaOS central configuration (dot-sourced once, from robo.ps1).
#
# Single source of truth for every path, toolchain hint, QEMU setting and
# timeout used by the build/test/run/deploy pipeline. Nothing else in the
# pipeline may hardcode these.
#
# Precedence: command-line flag  >  environment override  >  default below.
# Environment overrides are named ROBO_<SECTION>_<KEY>, e.g.
#   ROBO_QEMU_MEMORY_MB=1024  ROBO_QEMU_ACCEL=whpx  ROBO_RUNTIME_CONFIGURATION=Release
#   ROBO_TIMEOUT_KERNEL=600   ROBO_CLANG=C:\path\to\clang.exe

$script:RoboRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Get-RoboEnv {
    param([string]$Name, $Default)
    $v = [Environment]::GetEnvironmentVariable($Name)
    if ([string]::IsNullOrWhiteSpace($v)) { return $Default }
    return $v
}

function Get-RoboEnvInt {
    param([string]$Name, [int]$Default)
    $v = Get-RoboEnv $Name ""
    if ($v -eq "") { return $Default }
    $n = 0
    if ([int]::TryParse($v, [ref]$n)) { return $n }
    throw ("$Name must be an integer, got '$v'")
}

# --------------------------------------------------------------------------
# Toolchain
# --------------------------------------------------------------------------

# Windows LLVM install searched after PATH. Overridable so a developer with a
# different LLVM layout does not have to edit this file.
$script:RoboLlvmHint = Get-RoboEnv "ROBO_LLVM_BIN" "C:\CLang\clang+llvm-18.1.8-x86_64-pc-windows-msvc\bin"

# Optional explicit tool paths (checked first).
$script:RoboToolOverride = @{
    clang      = (Get-RoboEnv "ROBO_CLANG" "")
    ld_lld     = (Get-RoboEnv "ROBO_LD_LLD" "")
    lld_link   = (Get-RoboEnv "ROBO_LLD_LINK" "")
    llvm_ar    = (Get-RoboEnv "ROBO_LLVM_AR" "")
    llvm_readobj = (Get-RoboEnv "ROBO_LLVM_READOBJ" "")
    python     = (Get-RoboEnv "ROBO_PYTHON" "")
    qemu       = (Get-RoboEnv "ROBO_QEMU" "")
}

# --------------------------------------------------------------------------
# Repository layout: source roots (read-only inputs)
# --------------------------------------------------------------------------

$script:RoboPaths = @{
    Root        = $script:RoboRoot
    Bootloader  = (Join-Path $script:RoboRoot "bootloader")
    Kernel      = (Join-Path $script:RoboRoot "kernel")
    Libc        = (Join-Path $script:RoboRoot "libc")
    User        = (Join-Path $script:RoboRoot "user")
    Runtime     = (Join-Path $script:RoboRoot "runtime")
    Tools       = (Join-Path $script:RoboRoot "tools")
    Scripts     = (Join-Path $script:RoboRoot "scripts")
    Docs        = (Join-Path $script:RoboRoot "docs")
}

# --------------------------------------------------------------------------
# Build output: everything generated lives under build/ (never in sources).
# --------------------------------------------------------------------------

$script:RoboBuild = Join-Path $script:RoboRoot "build"

$script:RoboOut = @{
    Build       = $script:RoboBuild
    Artifacts   = Join-Path $script:RoboBuild "artifacts"
    Images      = Join-Path $script:RoboBuild "images"
    Deploy      = Join-Path $script:RoboBuild "deploy"
    Dist        = Join-Path $script:RoboBuild "dist"
    Logs        = Join-Path $script:RoboBuild "logs"
    Results     = Join-Path $script:RoboBuild "test-results"
    Temp        = Join-Path $script:RoboBuild "tmp"
    Bootloader  = Join-Path (Join-Path $script:RoboRoot "bootloader") "build"
    Kernel      = Join-Path (Join-Path $script:RoboRoot "kernel") "build"
    Libc        = Join-Path (Join-Path $script:RoboRoot "libc") "build"
    Runtime     = Join-Path (Join-Path $script:RoboRoot "runtime") "Out"
}

# Real, buildable artifacts (single place they are named).
$script:RoboArtifact = @{
    Efi            = Join-Path $script:RoboOut.Bootloader "BOOTX64.EFI"
    BootHost       = Join-Path $script:RoboOut.Bootloader "roboboot.exe"
    BootTest       = Join-Path $script:RoboOut.Bootloader "roboboot-test.exe"
    EspImage       = Join-Path $script:RoboOut.Bootloader "roboboot-esp.img"
    KernelElf      = Join-Path $script:RoboOut.Kernel "kernel.elf"
    KernelShipped  = Join-Path (Join-Path $script:RoboPaths.Bootloader "volumes\sys") "kernel.elf"
    Initrd         = Join-Path (Join-Path $script:RoboPaths.Bootloader "volumes\sys") "initrd"
    RuntimeTests   = Join-Path $script:RoboOut.Runtime "RoboTest.host.exe"
    DeployImage    = Join-Path $script:RoboOut.Images "roboticelaos.img"
    DeployManifest = Join-Path $script:RoboOut.Deploy "deployment.json"
}

# --------------------------------------------------------------------------
# Targets
# --------------------------------------------------------------------------

$script:RoboConfig = @{
    TargetArch    = "x86_64"
    RuntimeConfig = (Get-RoboEnv "ROBO_RUNTIME_CONFIGURATION" "Debug")
}

# --------------------------------------------------------------------------
# QEMU
# --------------------------------------------------------------------------

$script:RoboQemu = @{
    MemoryMB       = (Get-RoboEnvInt "ROBO_QEMU_MEMORY_MB" 512)
    # WHPX + std VGA GOP aborts QEMU on some Windows hosts, so tcg is the
    # deterministic default; override with ROBO_QEMU_ACCEL=whpx when it works.
    Accel          = (Get-RoboEnv "ROBO_QEMU_ACCEL" "tcg")
    Machine        = (Get-RoboEnv "ROBO_QEMU_MACHINE" "q35,usb=off")
    Cpu            = (Get-RoboEnv "ROBO_QEMU_CPU" "qemu64,-svm")
    Vga            = (Get-RoboEnv "ROBO_QEMU_VGA" "std")
    Display        = (Get-RoboEnv "ROBO_QEMU_DISPLAY" "gtk,show-menubar=off,grab-on-hover=on")
    HeadlessDisplay = "none"
    DiskMode       = (Get-RoboEnv "ROBO_QEMU_DISK" "ide")
    FirmwareCode   = (Get-RoboEnv "ROBO_OVMF_CODE" (Join-Path ${env:ProgramFiles} "qemu\share\edk2-x86_64-code.fd"))
    FirmwareVars   = (Get-RoboEnv "ROBO_OVMF_VARS" (Join-Path ${env:ProgramFiles} "qemu\share\edk2-i386-vars.fd"))
    NetPorts       = @(18019, 18080)
}

# --------------------------------------------------------------------------
# Timeouts (seconds) and image geometry
# --------------------------------------------------------------------------

$script:RoboTimeout = @{
    Build        = (Get-RoboEnvInt "ROBO_TIMEOUT_BUILD" 1800)
    Bootloader   = (Get-RoboEnvInt "ROBO_TIMEOUT_BOOTLOADER" 180)
    Kernel       = (Get-RoboEnvInt "ROBO_TIMEOUT_KERNEL" 420)
    Runtime      = (Get-RoboEnvInt "ROBO_TIMEOUT_RUNTIME" 300)
    Tests        = (Get-RoboEnvInt "ROBO_TIMEOUT_TESTS" 1800)
}

$script:RoboImage = @{
    EspSizeMB = (Get-RoboEnvInt "ROBO_ESP_SIZE_MB" 64)
}

# --------------------------------------------------------------------------
# Run flags (set by the command layer before it calls into a module)
# --------------------------------------------------------------------------

$script:RoboFlag = @{
    Rebuild    = $false
    SkipBuild  = $false
    SkipQemu   = $false
    Verbose    = $false
    Debug      = $false
    IncludeBaked = $false
    Headless   = $false
    FirmwareMenu = $false
    ResetNvram = $false
    Menu       = $false
    HoldAlt    = $false
    Visible    = $false
    Sanitize   = $false
    Force      = $false
    Filter     = ""
    Device     = ""
    AssumeYes  = $false
}

# Interactive-run settings (window geometry, how long the desktop window stays up).
$script:RoboRun = @{
    Width  = (Get-RoboEnvInt "ROBO_WINDOW_WIDTH" 1280)
    Height = (Get-RoboEnvInt "ROBO_WINDOW_HEIGHT" 800)
    HoldMs = (Get-RoboEnvInt "ROBO_DESKTOP_HOLD_MS" 0)
    Shot   = (Get-RoboEnv "ROBO_DESKTOP_SHOT" "")
}

function Initialize-RoboOutputTree {
    foreach ($dir in @(
        $script:RoboOut.Artifacts, $script:RoboOut.Images, $script:RoboOut.Deploy,
        $script:RoboOut.Dist, $script:RoboOut.Logs, $script:RoboOut.Results,
        $script:RoboOut.Temp
    )) {
        if (-not (Test-Path -LiteralPath $dir)) {
            New-Item -ItemType Directory -Force -Path $dir | Out-Null
        }
    }
}

$script:RoboSecretPattern = '(?i)(secret|password|passwd|token|credential|apikey|api_key|private_key)'

function Get-RoboRedactedEnv {
    $names = @(
        "ROBO_LLVM_BIN", "ROBO_CLANG", "ROBO_LD_LLD", "ROBO_LLD_LINK", "ROBO_LLVM_AR",
        "ROBO_PYTHON", "ROBO_QEMU", "ROBO_QEMU_ACCEL", "ROBO_QEMU_MEMORY_MB",
        "ROBO_RUNTIME_CONFIGURATION", "ROBO_OVMF_CODE", "ROBO_OVMF_VARS"
    )
    $rows = @()
    foreach ($n in $names) {
        $v = [Environment]::GetEnvironmentVariable($n)
        if (-not [string]::IsNullOrWhiteSpace($v)) {
            $rows += ("{0}={1}" -f $n, $v)
        }
    }
    return $rows
}

function Show-RoboConfiguration {
    Write-RoboSection "Configuration"
    Write-RoboKeyValue "Repository root" $script:RoboPaths.Root
    Write-RoboKeyValue "Target" ("{0} / {1}" -f $script:RoboConfig.TargetArch, $script:RoboConfig.RuntimeConfig)
    Write-RoboKeyValue "Build output" $script:RoboOut.Build
    Write-RoboKeyValue "Deployment tree" $script:RoboOut.Dist
    Write-RoboKeyValue "Deployment image" $script:RoboArtifact.DeployImage
    Write-RoboKeyValue "Logs" $script:RoboOut.Logs
    Write-RoboKeyValue "Test results" $script:RoboOut.Results
    Write-RoboKeyValue "QEMU" ("{0} MB, accel {1}, disk {2}, display {3}" -f
        $script:RoboQemu.MemoryMB, $script:RoboQemu.Accel, $script:RoboQemu.DiskMode, $script:RoboQemu.Display)
    Write-RoboKeyValue "Timeouts (s)" ("build {0}, boot {1}, kernel {2}, runtime {3}" -f
        $script:RoboTimeout.Build, $script:RoboTimeout.Bootloader, $script:RoboTimeout.Kernel, $script:RoboTimeout.Runtime)
    $envRows = Get-RoboRedactedEnv
    if ($envRows.Count -gt 0) {
        # Values are paths and numbers only; the pattern guard is belt-and-braces.
        foreach ($row in $envRows) {
            if ($row -match $script:RoboSecretPattern) { $row = ($row -split "=")[0] + "=<redacted>" }
            Write-RoboKeyValue "env" $row
        }
    }
}
