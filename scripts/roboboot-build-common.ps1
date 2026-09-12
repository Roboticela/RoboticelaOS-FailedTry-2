# RoboBoot / RoboticelaOS build engine.
# Shared by configure, build, host run, tests, and QEMU scripts.

$script:Repo = Split-Path -Parent $PSScriptRoot
$script:Boot = Join-Path $script:Repo "bootloader"
$script:Build = Join-Path $script:Boot "build"
$script:Dist = Join-Path $script:Repo "build"
$script:KernelRoot = Join-Path $script:Repo "kernel"
$script:KernelOut = Join-Path $script:KernelRoot "build\kernel.elf"
$script:PmmTestOut = Join-Path $script:KernelRoot "build\test_pmm.exe"
$script:VmmTestOut = Join-Path $script:KernelRoot "build\test_vmm.exe"
$script:TimeTestOut = Join-Path $script:KernelRoot "build\test_time.exe"
$script:SchedQueueTestOut = Join-Path $script:KernelRoot "build\test_sched_queue.exe"
$script:ThreadStateTestOut = Join-Path $script:KernelRoot "build\test_thread_state.exe"
$script:ProcessStateTestOut = Join-Path $script:KernelRoot "build\test_process_state.exe"
$script:ProcessHostTestOut = Join-Path $script:KernelRoot "build\test_process_host.exe"
$script:ContextLayoutTestOut = Join-Path $script:KernelRoot "build\test_context_layout.exe"
$script:ElfTestOut = Join-Path $script:KernelRoot "build\test_elf.exe"
$script:RfsTestOut = Join-Path $script:KernelRoot "build\test_rfs.exe"
$script:DriverSigTestOut = Join-Path $script:KernelRoot "build\test_driver_sig.exe"
$script:ObjHost = Join-Path $script:Build "obj\host"
$script:ObjEfi = Join-Path $script:Build "obj\efi"
$script:ObjTest = Join-Path $script:Build "obj\test"
$script:HostOut = Join-Path $script:Build "roboboot.exe"
$script:TestOut = Join-Path $script:Build "roboboot-test.exe"
$script:EfiOut = Join-Path $script:Build "BOOTX64.EFI"
$script:EspImg = Join-Path $script:Build "roboboot-esp.img"
$script:ConfigFile = Join-Path $script:Build "config"
$script:VarsFd = Join-Path $script:Build "ovmf-vars.fd"
$script:SerialLog = Join-Path $script:Build "qemu-serial.log"
$script:OvmfDebug = Join-Path $script:Build "ovmf-debug.log"
$script:EspFlagFile = Join-Path $script:Build "esp.flags"
$script:RoboRebuild = $false
$script:RoboBakedThisRun = $false

function Get-RoboSources([string]$Kind) {
    $core = [string[]]@(
        "src\rb_util.c",
        "src\rb_crypto.c",
        "src\rb_gfx.c",
        "src\rb_image.c",
        "src\rb_manifest.c",
        "src\rb_env.c",
        "src\rb_probe.c",
        "src\rb_loader.c",
        "src\rb_policy.c",
        "src\rb_ui.c",
        "src\rb_session.c"
    )
    $extra = @{
        host = [string[]]@("src\host\main_host.c")
        test = [string[]]@("tests\test_roboboot.c")
        efi  = [string[]]@("src\efi\efi_crt.c", "src\efi\efi_plat.c", "src\efi\efi_bootmgr.c", "src\efi\efi_kexec.c", "src\efi\main_efi.c")
    }
    if (-not $extra.ContainsKey($Kind)) { throw "unknown source set: $Kind" }
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($c in $core) { [void]$out.Add($c) }
    foreach ($e in $extra[$Kind]) { [void]$out.Add($e) }
    if ($out.Count -lt 12) { throw "source list for $Kind is too short ($($out.Count))" }
    return $out
}

function Get-RoboVersion {
    $h = Join-Path $script:Boot "include\roboboot.h"
    if (Test-Path $h) {
        $m = Select-String -Path $h -Pattern '#define\s+RB_VERSION_STR\s+"([^"]+)"' | Select-Object -First 1
        if ($m) { return $m.Matches[0].Groups[1].Value }
    }
    return "0.0.0"
}

function Get-Clang {
    $clang = Get-Command clang -ErrorAction SilentlyContinue
    if ($clang) { return $clang.Source }
    $hint = "C:\CLang\clang+llvm-18.1.8-x86_64-pc-windows-msvc\bin\clang.exe"
    if (Test-Path $hint) { return $hint }
    throw "clang not found (install LLVM and add it to PATH)"
}

function Get-LdLld {
    $ld = Get-Command ld.lld -ErrorAction SilentlyContinue
    if ($ld) { return $ld.Source }
    $dir = Split-Path (Get-LldLink)
    foreach ($name in @("ld.lld.exe", "ld.lld")) {
        $hint = Join-Path $dir $name
        if (Test-Path $hint) { return $hint }
    }
    $hint = "C:\CLang\clang+llvm-18.1.8-x86_64-pc-windows-msvc\bin\ld.lld.exe"
    if (Test-Path $hint) { return $hint }
    throw "ld.lld not found (needed to link kernel.elf)"
}

function Get-LldLink {
    $lld = Get-Command lld-link -ErrorAction SilentlyContinue
    if ($lld) { return $lld.Source }
    $hint = "C:\CLang\clang+llvm-18.1.8-x86_64-pc-windows-msvc\bin\lld-link.exe"
    if (Test-Path $hint) { return $hint }
    throw "lld-link not found (LLVM lld is required for the UEFI image)"
}

function Get-Python {
    $py = Get-Command python -ErrorAction SilentlyContinue
    if ($py) { return $py.Source }
    $py = Get-Command python3 -ErrorAction SilentlyContinue
    if ($py) { return $py.Source }
    throw "python not found (3.x with Pillow is required to bake assets)"
}

function Get-ToolVersion([string]$Exe, [string]$Arg = "--version") {
    try {
        $line = & $Exe $Arg 2>&1 | Select-Object -First 1
        return ($line -replace '^\s+', '').ToString()
    } catch {
        return $Exe
    }
}

function Get-HeaderDeps([string[]]$Dirs) {
    $h = @()
    foreach ($d in $Dirs) {
        if (Test-Path $d) {
            $h += @(Get-ChildItem $d -Filter *.h -File | ForEach-Object { $_.FullName })
        }
    }
    return $h
}

function Test-Stale([string]$Source, [string]$Object, [string[]]$Deps) {
    if (-not (Test-Path $Object)) { return $true }
    $objTime = (Get-Item $Object).LastWriteTimeUtc
    if ((Get-Item $Source).LastWriteTimeUtc -gt $objTime) { return $true }
    foreach ($d in $Deps) {
        if ($d -and (Test-Path $d) -and (Get-Item $d).LastWriteTimeUtc -gt $objTime) { return $true }
    }
    return $false
}

function Test-LinkStale([string]$Out, [string[]]$Objects) {
    if (-not (Test-Path $Out)) { return $true }
    $outTime = (Get-Item $Out).LastWriteTimeUtc
    foreach ($o in $Objects) {
        if (-not (Test-Path $o)) { return $true }
        if ((Get-Item $o).LastWriteTimeUtc -gt $outTime) { return $true }
    }
    return $false
}

function Write-Step([string]$Stage, [string]$Msg) {
    Write-Host ("  {0,-7} {1}" -f $Stage, $Msg)
}

function Resolve-BootRel([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path)
    $root = [System.IO.Path]::GetFullPath($script:Boot)
    if ($full.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($root.Length).TrimStart('\', '/')
    }
    return $Path
}

function Invoke-Cc {
    param(
        [string]$Clang,
        [string[]]$CFlags,
        [string]$Source,
        [string]$Object,
        [string[]]$Deps
    )
    if (-not $script:RoboRebuild -and -not (Test-Stale $Source $Object $Deps)) {
        return $false
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $Object) | Out-Null
    Write-Step "CC" (Resolve-BootRel $Source)
    & $Clang @CFlags -c $Source -o $Object
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $(Resolve-BootRel $Source)" }
    return $true
}

function Write-RoboConfig {
    New-Item -ItemType Directory -Force -Path $script:Build | Out-Null
    $clang = Get-Clang
    $py = Get-Python
    $lld = $null
    try { $lld = Get-LldLink } catch { $lld = "" }
    $lines = @(
        "ROBOBOOT_VERSION=$(Get-RoboVersion)",
        "HOST_CC=$clang",
        "HOST_CC_VERSION=$(Get-ToolVersion $clang)",
        "PYTHON=$py",
        "PYTHON_VERSION=$(Get-ToolVersion $py -Arg '-V')",
        "EFI_TRIPLE=x86_64-unknown-windows",
        "HOST_TRIPLE=x86_64-pc-windows-msvc",
        "OUT=$script:Build"
    )
    if ($lld) {
        $lines += "EFI_LD=$lld"
        $lines += "EFI_LD_VERSION=$(Get-ToolVersion $lld)"
    }
    Set-Content -Path $script:ConfigFile -Value $lines -Encoding ascii
}

function Show-RoboBuildInfo {
    $ver = Get-RoboVersion
    $clang = Get-Clang
    $py = Get-Python
    Write-Host ""
    Write-Host "  RoboticelaOS  RoboBoot $ver"
    Write-Host "  --------------------------------"
    Write-Host "  CC       $(Get-ToolVersion $clang)"
    Write-Host "  PYTHON   $(Get-ToolVersion $py -Arg '-V')"
    try {
        $lld = Get-LldLink
        Write-Host "  LD       $(Get-ToolVersion $lld)"
        Write-Host "  EFI      x86_64-unknown-windows"
    } catch {
        Write-Host "  LD       (missing - UEFI image will not build)"
    }
    Write-Host "  PREVIEW  x86_64-pc-windows-msvc"
    Write-Host "  OUT      $script:Build"
    Write-Host "  DIST     $script:Dist"
    Write-Host ""
}

function Invoke-RoboBake {
    if ($script:RoboBakedThisRun) { return }
    $py = Get-Python
    $scriptPath = Join-Path $script:Boot "tools\bake_assets.py"
    $required = @(
        (Join-Path $script:Boot "assets\logo.rbim"),
        (Join-Path $script:Boot "assets\fonts\ui-title.rbfn"),
        (Join-Path $script:Boot "assets\fonts\ui-body.rbfn"),
        (Join-Path $script:Boot "assets\fonts\ui-caption.rbfn")
    )
    $inputs = @(
        $scriptPath,
        (Join-Path $script:Boot "assets\logo.svg"),
        (Join-Path $script:Boot "assets\HDD.png"),
        (Join-Path $script:Boot "assets\SSD.png"),
        (Join-Path $script:Boot "assets\NVMe.png"),
        (Join-Path $script:Boot "assets\USB.png")
    )
    $needed = $script:RoboRebuild
    if (-not $needed) {
        foreach ($o in $required) {
            if (-not (Test-Path $o)) { $needed = $true; break }
        }
    }
    if (-not $needed) {
        $oldest = [datetime]::MaxValue
        foreach ($o in $required) {
            $t = (Get-Item $o).LastWriteTimeUtc
            if ($t -lt $oldest) { $oldest = $t }
        }
        foreach ($i in $inputs) {
            if ((Test-Path $i) -and (Get-Item $i).LastWriteTimeUtc -gt $oldest) {
                $needed = $true
                break
            }
        }
    }
    if (-not $needed) {
        $script:RoboBakedThisRun = $true
        return
    }
    Write-Step "BAKE" "assets (fonts, pictures, dummy volumes)"
    & $py $scriptPath
    if ($LASTEXITCODE -ne 0) { throw "asset bake failed" }
    $script:RoboBakedThisRun = $true
}

function Invoke-RoboBuildHost {
    Invoke-RoboBake
    Write-Step "HOST" "native preview"
    New-Item -ItemType Directory -Force -Path $script:ObjHost | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2", "-g",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-I", (Join-Path $script:Boot "include")
    )
    $deps = Get-HeaderDeps @((Join-Path $script:Boot "include"))
    $objs = @()
    $compiled = $false
    foreach ($rel in (Get-RoboSources "host")) {
        $src = Join-Path $script:Boot $rel
        $obj = Join-Path $script:ObjHost (([IO.Path]::GetFileNameWithoutExtension($rel)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:HostOut $objs)) {
        Write-Step "LD" "build\roboboot.exe"
        & $clang -g -o $script:HostOut @objs -luser32 -lgdi32
        if ($LASTEXITCODE -ne 0) { throw "host link failed" }
    }
}

function Invoke-RoboBuildTest {
    Invoke-RoboBake
    Write-Step "TEST" "unit tests"
    New-Item -ItemType Directory -Force -Path $script:ObjTest | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-I", (Join-Path $script:Boot "include")
    )
    $deps = Get-HeaderDeps @((Join-Path $script:Boot "include"))
    $objs = @()
    $compiled = $false
    foreach ($rel in (Get-RoboSources "test")) {
        $src = Join-Path $script:Boot $rel
        $obj = Join-Path $script:ObjTest (([IO.Path]::GetFileNameWithoutExtension($rel)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:TestOut $objs)) {
        Write-Step "LD" "build\roboboot-test.exe"
        & $clang -o $script:TestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "test link failed" }
    }
}

function Invoke-RoboBuildEfi {
    Invoke-RoboBake
    Write-Step "EFI" "UEFI x64 application"
    New-Item -ItemType Directory -Force -Path $script:ObjEfi | Out-Null
    $clang = Get-Clang
    $lld = Get-LldLink
    $cflags = @(
        "-target", "x86_64-unknown-windows",
        "-std=c11", "-O2",
        "-ffreestanding", "-fno-stack-protector", "-fshort-wchar",
        "-mno-red-zone", "-fno-builtin",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-I", (Join-Path $script:Boot "include"),
        "-I", (Join-Path $script:Boot "src\efi\libc"),
        "-I", (Join-Path $script:Boot "src\efi")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $script:Boot "include"),
        (Join-Path $script:Boot "include\roboticela"),
        (Join-Path $script:Boot "src\efi"),
        (Join-Path $script:Boot "src\efi\libc")
    )
    $objs = @()
    $compiled = $false
    foreach ($rel in (Get-RoboSources "efi")) {
        $src = Join-Path $script:Boot $rel
        $obj = Join-Path $script:ObjEfi (([IO.Path]::GetFileNameWithoutExtension($rel)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:EfiOut $objs)) {
        Write-Step "LD" "build\BOOTX64.EFI"
        & $lld /subsystem:EFI_APPLICATION /entry:EfiMain /out:$script:EfiOut @objs
        if ($LASTEXITCODE -ne 0) { throw "EFI link failed" }
    }
}

function Get-EspFlagKey([string]$Flags) {
    $parts = @()
    foreach ($line in ($Flags -replace ",", "`n" -split "`n")) {
        $t = $line.Trim()
        if ($t) { $parts += $t }
    }
    return ($parts -join "`n")
}

function Invoke-RoboBuildUserland {
    $kroot = $script:KernelRoot
    $user = Join-Path $script:Repo "user"
    $ubuild = Join-Path $kroot "build\user"
    New-Item -ItemType Directory -Force -Path $ubuild | Out-Null
    $clang = Get-Clang
    $ld = Get-LdLld
    $ldscript = Join-Path $user "linker.ld"
    $sdk = Join-Path $script:Repo "sdk\include"
    $cflags = @(
        "-target", "x86_64-unknown-none",
        "-std=c11", "-O2",
        "-ffreestanding", "-nostdlib", "-static",
        "-fno-pic", "-fno-pie", "-fno-stack-protector",
        "-fno-asynchronous-unwind-tables",
        "-mcmodel=large",
        "-mno-red-zone", "-fno-builtin",
        "-I", $sdk
    )
    $sdkdeps = Get-HeaderDeps @((Join-Path $sdk "robosys"))
    $progs = @(
        @{ Name = "hello"; Srcs = @("lib\crt0.S", "lib\syscall.S", "hello\hello.c") },
        @{ Name = "fault"; Srcs = @("fault\start.S") },
        @{ Name = "systest"; Srcs = @("lib\crt0.S", "lib\syscall.S", "systest\systest.c", "systest\regs.S", "systest\worker.S") },
        @{ Name = "a"; Srcs = @("lib\crt0.S", "lib\syscall.S", "iso\a.c") },
        @{ Name = "b"; Srcs = @("lib\crt0.S", "lib\syscall.S", "iso\b.c") },
        @{ Name = "fd-test"; Srcs = @("lib\crt0.S", "lib\syscall.S", "fd-test\fd-test.c") },
        @{ Name = "robofs-test"; Srcs = @("lib\crt0.S", "lib\syscall.S", "robofs-test\robofs-test.c") },
        @{ Name = "robo"; Srcs = @("lib\crt0.S", "lib\syscall.S", "robo\main.c") },
        @{ Name = "init"; Srcs = @("lib\crt0.S", "lib\syscall.S", "init\main.c", "init\service.c", "init\config.c") },
        @{ Name = "test-service"; Srcs = @("lib\crt0.S", "lib\syscall.S", "services\test-service\test-service.c") },
        @{ Name = "crash-test"; Srcs = @("services\crash-test\start.S") },
        @{ Name = "isol-test"; Srcs = @("services\isol-test\start.S") }
    )
    $elfs = @{}
    foreach ($p in $progs) {
        $objs = @()
        $i = 0
        foreach ($rel in $p.Srcs) {
            $src = Join-Path $user $rel
            $obj = Join-Path $ubuild ($p.Name + "_" + $i + ".o")
            if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps (@($ldscript) + $sdkdeps)) { }
            $objs += $obj
            $i++
        }
        $elf = Join-Path $ubuild ($p.Name + ".elf")
        Write-Step "LD" ("user\" + $p.Name + ".elf")
        & $ld "-m" "elf_x86_64" "-static" "-no-pie" "-T" $ldscript "-o" $elf @objs
        if ($LASTEXITCODE -ne 0) { throw ("user " + $p.Name + " link failed") }
        $elfs[$p.Name] = $elf
    }
    $py = Get-Python
    $pack = Join-Path $user "pack_robofs.py"
    $initrd = Join-Path $script:Boot "volumes\sys\initrd"
    New-Item -ItemType Directory -Force -Path (Split-Path $initrd) | Out-Null
    $txt = Join-Path $ubuild "test.txt"
    [IO.File]::WriteAllBytes($txt, [Text.Encoding]::ASCII.GetBytes("Roboticela OS"))
    Write-Step "FS" "volumes\sys\initrd"
    & $py $pack "-o" $initrd (
        ("/bin/hello=" + $elfs["hello"]),
        ("/bin/fault=" + $elfs["fault"]),
        ("/bin/systest=" + $elfs["systest"]),
        ("/bin/a=" + $elfs["a"]),
        ("/bin/b=" + $elfs["b"]),
        ("/bin/fd-test=" + $elfs["fd-test"]),
        ("/bin/robofs-test=" + $elfs["robofs-test"]),
        ("/bin/robo=" + $elfs["robo"]),
        ("/System/init=" + $elfs["init"]),
        ("/System/services/test-service=" + $elfs["test-service"]),
        ("/System/services/crash-test=" + $elfs["crash-test"]),
        ("/System/services/isol-test=" + $elfs["isol-test"]),
        ("/test.txt=" + $txt)
    )
    if ($LASTEXITCODE -ne 0) { throw "RoboFS pack failed" }
}

function Invoke-RoboBuildKernel {
    $kroot = $script:KernelRoot
    $kbuild = Join-Path $kroot "build"
    $kobj = Join-Path $kbuild "obj"
    $kout = $script:KernelOut
    $kfont = Join-Path $kroot "src\kfont.c"
    $sys = Join-Path $script:Boot "volumes\sys\kernel.elf"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $ld = Get-LdLld
    $py = Get-Python
    if ($script:RoboRebuild -or -not (Test-Path $kfont)) {
        Write-Step "FONT" "kernel console glyphs"
        & $py (Join-Path $kroot "tools\gen_font.py")
        if ($LASTEXITCODE -ne 0) { throw "kernel font generate failed" }
    }
    $cflags = @(
        "-target", "x86_64-unknown-none",
        "-std=c11", "-O2",
        "-ffreestanding", "-fno-stack-protector", "-fno-pic", "-fno-pie",
        "-mno-red-zone", "-mgeneral-regs-only", "-fno-builtin", "-nostdlib",
        "-I", (Join-Path $kroot "include"),
        "-I", $kroot,
        "-I", (Join-Path $script:Boot "include"),
        "-I", (Join-Path $script:Repo "sdk\include")
    )
    $srcs = @(
        (Join-Path $kroot "arch\x86_64\boot\start.S"),
        (Join-Path $kroot "arch\x86_64\isr.S"),
        (Join-Path $kroot "arch\x86_64\context\context_switch.S"),
        (Join-Path $kroot "arch\x86_64\syscall\syscall_entry.S"),
        (Join-Path $kroot "arch\x86_64\context\context.c"),
        (Join-Path $kroot "arch\x86_64\context\context_test.c"),
        (Join-Path $kroot "arch\x86_64\gdt.c"),
        (Join-Path $kroot "arch\x86_64\tss.c"),
        (Join-Path $kroot "arch\x86_64\idt.c"),
        (Join-Path $kroot "arch\x86_64\exception.c"),
        (Join-Path $kroot "arch\x86_64\pf_test.c"),
        (Join-Path $kroot "arch\x86_64\cpu.c"),
        (Join-Path $kroot "arch\x86_64\cpu_test.c"),
        (Join-Path $kroot "arch\x86_64\irq.c"),
        (Join-Path $kroot "arch\x86_64\syscall\syscall_cpu.c"),
        (Join-Path $kroot "syscall\syscall.c"),
        (Join-Path $kroot "syscall\syscall_dispatch.c"),
        (Join-Path $kroot "syscall\syscall_table.c"),
        (Join-Path $kroot "syscall\syscall_validate.c"),
        (Join-Path $kroot "syscall\syscall_debug.c"),
        (Join-Path $kroot "arch\x86_64\timer\tsc.c"),
        (Join-Path $kroot "arch\x86_64\timer\pit.c"),
        (Join-Path $kroot "time\convert.c"),
        (Join-Path $kroot "time\time.c"),
        (Join-Path $kroot "time\time_test.c"),
        (Join-Path $kroot "scheduler\queue.c"),
        (Join-Path $kroot "scheduler\task.c"),
        (Join-Path $kroot "scheduler\scheduler.c"),
        (Join-Path $kroot "scheduler\sched_test.c"),
        (Join-Path $kroot "thread\thread.c"),
        (Join-Path $kroot "thread\thread_test.c"),
        (Join-Path $kroot "process\process.c"),
        (Join-Path $kroot "process\process_table.c"),
        (Join-Path $kroot "process\process_resources.c"),
        (Join-Path $kroot "process\process_debug.c"),
        (Join-Path $kroot "process\process_test.c"),
        (Join-Path $kroot "process\init.c"),
        (Join-Path $kroot "exec\elf\elf.c"),
        (Join-Path $kroot "exec\elf\elf_parser.c"),
        (Join-Path $kroot "exec\elf\elf_validate.c"),
        (Join-Path $kroot "exec\elf\elf_loader.c"),
        (Join-Path $kroot "exec\exec.c"),
        (Join-Path $kroot "exec\exec_test.c"),
        (Join-Path $kroot "fs\robofs.c"),
        (Join-Path $kroot "fs\file.c"),
        (Join-Path $kroot "fs\file_ops.c"),
        (Join-Path $kroot "fs\fd_table.c"),
        (Join-Path $kroot "fs\fd.c"),
        (Join-Path $kroot "fs\backends\console.c"),
        (Join-Path $kroot "fs\backends\regular.c"),
        (Join-Path $kroot "fs\backends\pipe.c"),
        (Join-Path $kroot "fs\backends\device.c"),
        (Join-Path $kroot "fs\vfs\vnode.c"),
        (Join-Path $kroot "fs\vfs\path.c"),
        (Join-Path $kroot "fs\vfs\mount.c"),
        (Join-Path $kroot "fs\vfs\vfs.c"),
        (Join-Path $kroot "fs\vfs\vfs_test.c"),
        (Join-Path $kroot "fs\block\memdev.c"),
        (Join-Path $kroot "fs\rfs\rfs.c"),
        (Join-Path $kroot "fs\rfs\rfs_test.c"),
        (Join-Path $kroot "arch\x86_64\acpi\acpi.c"),
        (Join-Path $kroot "arch\x86_64\mmio.c"),
        (Join-Path $kroot "pci\pci.c"),
        (Join-Path $kroot "device\model.c"),
        (Join-Path $kroot "drivers\registry.c"),
        (Join-Path $kroot "drivers\package.c"),
        (Join-Path $kroot "drivers\manager.c"),
        (Join-Path $kroot "drivers\signature\sha256.c"),
        (Join-Path $kroot "drivers\signature\sha512.c"),
        (Join-Path $kroot "drivers\signature\ed25519.c"),
        (Join-Path $kroot "drivers\builtin\fb_generic.c"),
        (Join-Path $kroot "storage\virtio_blk.c"),
        (Join-Path $kroot "graphics\gfx.c"),
        (Join-Path $kroot "input\input.c"),
        (Join-Path $kroot "input\ps2.c"),
        (Join-Path $kroot "src\kserial.c"),
        (Join-Path $kroot "src\kfb.c"),
        (Join-Path $kroot "src\kboot.c"),
        (Join-Path $kroot "src\kernel.c"),
        (Join-Path $kroot "mm\page.c"),
        (Join-Path $kroot "mm\memory_map.c"),
        (Join-Path $kroot "mm\pmm.c"),
        (Join-Path $kroot "mm\pmm_test.c"),
        (Join-Path $kroot "mm\vmm\paging.c"),
        (Join-Path $kroot "mm\vmm\table.c"),
        (Join-Path $kroot "mm\vmm\vmm.c"),
        (Join-Path $kroot "mm\vmm\vmm_test.c"),
        (Join-Path $kroot "mm\heap\heap.c"),
        (Join-Path $kroot "mm\heap\heap_test.c"),
        (Join-Path $kroot "arch\x86_64\mm\paging_cpu.c"),
        (Join-Path $kroot "arch\x86_64\mm\aspace.c"),
        $kfont
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "include"),
        (Join-Path $kroot "mm"),
        (Join-Path $kroot "mm\vmm"),
        (Join-Path $kroot "mm\heap"),
        (Join-Path $kroot "arch\x86_64"),
        (Join-Path $kroot "arch\x86_64\mm"),
        (Join-Path $kroot "arch\x86_64\timer"),
        (Join-Path $kroot "arch\x86_64\scheduler"),
        (Join-Path $kroot "arch\x86_64\context"),
        (Join-Path $kroot "arch\x86_64\syscall"),
        (Join-Path $kroot "time"),
        (Join-Path $kroot "scheduler"),
        (Join-Path $kroot "thread"),
        (Join-Path $kroot "process"),
        (Join-Path $kroot "exec"),
        (Join-Path $kroot "exec\elf"),
        (Join-Path $kroot "fs"),
        (Join-Path $kroot "fs\backends"),
        (Join-Path $kroot "fs\vfs"),
        (Join-Path $kroot "fs\block"),
        (Join-Path $kroot "fs\rfs"),
        (Join-Path $kroot "syscall"),
        (Join-Path $kroot "pci"),
        (Join-Path $kroot "device"),
        (Join-Path $kroot "drivers"),
        (Join-Path $kroot "drivers\signature"),
        (Join-Path $kroot "drivers\builtin"),
        (Join-Path $kroot "storage"),
        (Join-Path $kroot "graphics"),
        (Join-Path $kroot "input"),
        (Join-Path $kroot "irq"),
        (Join-Path $kroot "arch\x86_64\acpi"),
        (Join-Path $script:Repo "sdk\include\robosys"),
        (Join-Path $script:Boot "include\roboticela")
    )
    $objs = @()
    $compiled = $false
    foreach ($src in $srcs) {
        $obj = Join-Path $kobj (([IO.Path]::GetFileNameWithoutExtension($src)) + ".o")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    $ldscript = Join-Path $kroot "linker\kernel.ld"
    $needLink = $script:RoboRebuild -or $compiled -or (Test-LinkStale $kout $objs)
    if (-not $needLink -and (Test-Path $ldscript) -and (Test-Path $kout)) {
        if ((Get-Item $ldscript).LastWriteTimeUtc -gt (Get-Item $kout).LastWriteTimeUtc) { $needLink = $true }
    }
    if ($needLink) {
        Write-Step "LD" "kernel\build\kernel.elf"
        & $ld "-m" "elf_x86_64" "-T" $ldscript "-o" $kout @objs
        if ($LASTEXITCODE -ne 0) { throw "kernel link failed" }
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $sys) | Out-Null
    Copy-Item $kout $sys -Force
    Write-Step "KERN" "volumes\sys\kernel.elf"
    Invoke-RoboBuildUserland
}

function Invoke-RoboBuildPmmTest {
    $kroot = $script:KernelRoot
    $kobj = Join-Path $kroot "build\obj\pmm-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-DPMM_HOST_TEST",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-I", $kroot,
        "-I", (Join-Path $kroot "include"),
        "-I", (Join-Path $script:Boot "include")
    )
    $srcs = @(
        (Join-Path $kroot "mm\page.c"),
        (Join-Path $kroot "mm\memory_map.c"),
        (Join-Path $kroot "mm\pmm.c"),
        (Join-Path $kroot "mm\pmm_test.c"),
        (Join-Path $kroot "tests\test_pmm.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "mm"),
        (Join-Path $kroot "include"),
        (Join-Path $script:Boot "include\roboticela")
    )
    $objs = @()
    $compiled = $false
    foreach ($src in $srcs) {
        $obj = Join-Path $kobj (([IO.Path]::GetFileNameWithoutExtension($src)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:PmmTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_pmm.exe"
        & $clang -o $script:PmmTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "pmm host test link failed" }
    }
}

function Invoke-RoboTestPmm {
    Invoke-RoboBuildPmmTest
    Write-Step "TEST" "test_pmm.exe"
    & $script:PmmTestOut
    if ($LASTEXITCODE -ne 0) { throw "pmm host tests failed" }
}

function Invoke-RoboBuildVmmTest {
    $kroot = $script:KernelRoot
    $kobj = Join-Path $kroot "build\obj\vmm-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-I", $kroot,
        "-I", (Join-Path $kroot "include"),
        "-I", (Join-Path $script:Boot "include")
    )
    $srcs = @(
        (Join-Path $kroot "mm\page.c"),
        (Join-Path $kroot "mm\vmm\paging.c"),
        (Join-Path $kroot "tests\test_vmm.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "mm"),
        (Join-Path $kroot "mm\vmm"),
        (Join-Path $kroot "include")
    )
    $objs = @()
    $compiled = $false
    foreach ($src in $srcs) {
        $obj = Join-Path $kobj (([IO.Path]::GetFileNameWithoutExtension($src)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:VmmTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_vmm.exe"
        & $clang -o $script:VmmTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "vmm host test link failed" }
    }
}

function Invoke-RoboTestVmm {
    Invoke-RoboBuildVmmTest
    Write-Step "TEST" "test_vmm.exe"
    & $script:VmmTestOut
    if ($LASTEXITCODE -ne 0) { throw "vmm host tests failed" }
}

function Invoke-RoboBuildTimeTest {
    $kroot = $script:KernelRoot
    $kobj = Join-Path $kroot "build\obj\time-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-I", $kroot,
        "-I", (Join-Path $kroot "include")
    )
    $srcs = @(
        (Join-Path $kroot "time\convert.c"),
        (Join-Path $kroot "tests\test_time.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "time")
    )
    $objs = @()
    $compiled = $false
    foreach ($src in $srcs) {
        $obj = Join-Path $kobj (([IO.Path]::GetFileNameWithoutExtension($src)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:TimeTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_time.exe"
        & $clang -o $script:TimeTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "time host test link failed" }
    }
}

function Invoke-RoboTestTime {
    Invoke-RoboBuildTimeTest
    Write-Step "TEST" "test_time.exe"
    & $script:TimeTestOut
    if ($LASTEXITCODE -ne 0) { throw "time host tests failed" }
}

function Invoke-RoboBuildSchedQueueTest {
    $kroot = $script:KernelRoot
    $kobj = Join-Path $kroot "build\obj\sched-queue-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-I", $kroot,
        "-I", (Join-Path $kroot "include")
    )
    $srcs = @(
        (Join-Path $kroot "scheduler\queue.c"),
        (Join-Path $kroot "tests\test_sched_queue.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "scheduler"),
        (Join-Path $kroot "thread"),
        (Join-Path $kroot "arch\x86_64"),
        (Join-Path $kroot "mm")
    )
    $objs = @()
    $compiled = $false
    foreach ($src in $srcs) {
        $obj = Join-Path $kobj (([IO.Path]::GetFileNameWithoutExtension($src)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:SchedQueueTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_sched_queue.exe"
        & $clang -o $script:SchedQueueTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "sched queue host test link failed" }
    }
}

function Invoke-RoboTestSchedQueue {
    Invoke-RoboBuildSchedQueueTest
    Write-Step "TEST" "test_sched_queue.exe"
    & $script:SchedQueueTestOut
    if ($LASTEXITCODE -ne 0) { throw "sched queue host tests failed" }
}

function Invoke-RoboBuildThreadStateTest {
    $kroot = $script:KernelRoot
    $kobj = Join-Path $kroot "build\obj\thread-state-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-I", $kroot
    )
    $srcs = @(
        (Join-Path $kroot "tests\test_thread_state.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "thread")
    )
    $objs = @()
    $compiled = $false
    foreach ($src in $srcs) {
        $obj = Join-Path $kobj (([IO.Path]::GetFileNameWithoutExtension($src)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:ThreadStateTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_thread_state.exe"
        & $clang -o $script:ThreadStateTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "thread state host test link failed" }
    }
}

function Invoke-RoboTestThreadState {
    Invoke-RoboBuildThreadStateTest
    Write-Step "TEST" "test_thread_state.exe"
    & $script:ThreadStateTestOut
    if ($LASTEXITCODE -ne 0) { throw "thread state host tests failed" }
}

function Invoke-RoboBuildProcessStateTest {
    $kroot = $script:KernelRoot
    $kobj = Join-Path $kroot "build\obj\process-state-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-I", $kroot
    )
    $srcs = @(
        (Join-Path $kroot "tests\test_process_state.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "process")
    )
    $objs = @()
    $compiled = $false
    foreach ($src in $srcs) {
        $obj = Join-Path $kobj (([IO.Path]::GetFileNameWithoutExtension($src)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:ProcessStateTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_process_state.exe"
        & $clang -o $script:ProcessStateTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "process state host test link failed" }
    }
}

function Invoke-RoboTestProcessState {
    Invoke-RoboBuildProcessStateTest
    Write-Step "TEST" "test_process_state.exe"
    & $script:ProcessStateTestOut
    if ($LASTEXITCODE -ne 0) { throw "process state host tests failed" }
}

function Invoke-RoboBuildProcessHostTest {
    $kroot = $script:KernelRoot
    $kobj = Join-Path $kroot "build\obj\process-host-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-DPROCESS_HOST_TEST",
        "-I", $kroot,
        "-I", (Join-Path $script:Repo "sdk\include")
    )
    $srcs = @(
        (Join-Path $kroot "tests\test_process_host.c"),
        (Join-Path $kroot "process\process.c"),
        (Join-Path $kroot "process\process_table.c"),
        (Join-Path $kroot "process\process_resources.c"),
        (Join-Path $kroot "fs\file.c"),
        (Join-Path $kroot "fs\file_ops.c"),
        (Join-Path $kroot "fs\fd_table.c"),
        (Join-Path $kroot "fs\fd.c"),
        (Join-Path $kroot "fs\backends\console.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "process"),
        (Join-Path $kroot "thread"),
        (Join-Path $kroot "mm"),
        (Join-Path $kroot "mm\vmm"),
        (Join-Path $kroot "fs"),
        (Join-Path $kroot "fs\backends"),
        (Join-Path $script:Repo "sdk\include\robosys")
    )
    $objs = @()
    $compiled = $false
    foreach ($src in $srcs) {
        $obj = Join-Path $kobj (([IO.Path]::GetFileNameWithoutExtension($src)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:ProcessHostTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_process_host.exe"
        & $clang -o $script:ProcessHostTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "process host test link failed" }
    }
}

function Invoke-RoboTestProcessHost {
    Invoke-RoboBuildProcessHostTest
    Write-Step "TEST" "test_process_host.exe"
    & $script:ProcessHostTestOut
    if ($LASTEXITCODE -ne 0) { throw "process host tests failed" }
}

function Invoke-RoboBuildContextLayoutTest {
    $kroot = $script:KernelRoot
    $kobj = Join-Path $kroot "build\obj\context-layout-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-DCONTEXT_HOST_TEST",
        "-I", $kroot
    )
    $srcs = @(
        (Join-Path $kroot "tests\test_context_layout.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "arch\x86_64"),
        (Join-Path $kroot "arch\x86_64\context")
    )
    $objs = @()
    $compiled = $false
    foreach ($src in $srcs) {
        $obj = Join-Path $kobj (([IO.Path]::GetFileNameWithoutExtension($src)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:ContextLayoutTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_context_layout.exe"
        & $clang -o $script:ContextLayoutTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "context layout host test link failed" }
    }
}

function Invoke-RoboTestContextLayout {
    Invoke-RoboBuildContextLayoutTest
    Write-Step "TEST" "test_context_layout.exe"
    & $script:ContextLayoutTestOut
    if ($LASTEXITCODE -ne 0) { throw "context layout host tests failed" }
}

function Invoke-RoboBuildElfTest {
    $kroot = $script:KernelRoot
    $kobj = Join-Path $kroot "build\obj\elf-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-I", $kroot,
        "-I", (Join-Path $kroot "include"),
        "-I", (Join-Path $script:Boot "include")
    )
    $srcs = @(
        (Join-Path $kroot "exec\elf\elf.c"),
        (Join-Path $kroot "exec\elf\elf_parser.c"),
        (Join-Path $kroot "exec\elf\elf_validate.c"),
        (Join-Path $kroot "mm\page.c"),
        (Join-Path $kroot "mm\vmm\paging.c"),
        (Join-Path $kroot "tests\test_elf.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "exec"),
        (Join-Path $kroot "exec\elf"),
        (Join-Path $kroot "mm"),
        (Join-Path $kroot "mm\vmm"),
        (Join-Path $kroot "include")
    )
    $objs = @()
    $compiled = $false
    foreach ($src in $srcs) {
        $obj = Join-Path $kobj (([IO.Path]::GetFileNameWithoutExtension($src)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:ElfTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_elf.exe"
        & $clang -o $script:ElfTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "elf host test link failed" }
    }
}

function Invoke-RoboTestElf {
    Invoke-RoboBuildElfTest
    Write-Step "TEST" "test_elf.exe"
    & $script:ElfTestOut
    if ($LASTEXITCODE -ne 0) { throw "elf host tests failed" }
}

function Invoke-RoboBuildRfsTest {
    $kroot = $script:KernelRoot
    $kobj = Join-Path $kroot "build\obj\rfs-host-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-DRFS_HOST_TEST",
        "-I", $kroot,
        "-I", (Join-Path $kroot "include"),
        "-I", (Join-Path $script:Repo "sdk\include")
    )
    $srcs = @(
        (Join-Path $kroot "tests\test_rfs.c"),
        (Join-Path $kroot "fs\file.c"),
        (Join-Path $kroot "fs\file_ops.c"),
        (Join-Path $kroot "fs\vfs\vnode.c"),
        (Join-Path $kroot "fs\vfs\path.c"),
        (Join-Path $kroot "fs\vfs\mount.c"),
        (Join-Path $kroot "fs\vfs\vfs.c"),
        (Join-Path $kroot "fs\vfs\vfs_test.c"),
        (Join-Path $kroot "fs\block\memdev.c"),
        (Join-Path $kroot "fs\rfs\rfs.c"),
        (Join-Path $kroot "fs\rfs\rfs_test.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "fs"),
        (Join-Path $kroot "fs\vfs"),
        (Join-Path $kroot "fs\block"),
        (Join-Path $kroot "fs\rfs"),
        (Join-Path $kroot "syscall"),
        (Join-Path $script:Repo "sdk\include\robosys")
    )
    $objs = @()
    $compiled = $false
    foreach ($src in $srcs) {
        $obj = Join-Path $kobj (([IO.Path]::GetFileNameWithoutExtension($src)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:RfsTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_rfs.exe"
        & $clang -o $script:RfsTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "rfs host test link failed" }
    }
}

function Invoke-RoboTestRfs {
    Invoke-RoboBuildRfsTest
    Write-Step "TEST" "test_rfs.exe"
    & $script:RfsTestOut
    if ($LASTEXITCODE -ne 0) { throw "rfs host tests failed" }
}

function Invoke-RoboBuildDriverSigTest {
    $kroot = $script:KernelRoot
    $kobj = Join-Path $kroot "build\obj\driver-sig-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-DDRIVER_HOST_TEST",
        "-I", $kroot,
        "-I", (Join-Path $kroot "include"),
        "-I", (Join-Path $script:Repo "sdk\include")
    )
    $srcs = @(
        (Join-Path $kroot "tests\test_driver_sig.c"),
        (Join-Path $kroot "drivers\registry.c"),
        (Join-Path $kroot "drivers\package.c"),
        (Join-Path $kroot "drivers\signature\sha256.c"),
        (Join-Path $kroot "drivers\signature\sha512.c"),
        (Join-Path $kroot "drivers\signature\ed25519.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "drivers"),
        (Join-Path $kroot "drivers\signature"),
        (Join-Path $kroot "device"),
        (Join-Path $kroot "pci"),
        (Join-Path $kroot "syscall"),
        (Join-Path $script:Repo "sdk\include\robosys")
    )
    $objs = @()
    $compiled = $false
    foreach ($src in $srcs) {
        $obj = Join-Path $kobj (([IO.Path]::GetFileNameWithoutExtension($src)) + ".obj")
        if (Invoke-Cc -Clang $clang -CFlags $cflags -Source $src -Object $obj -Deps $deps) { $compiled = $true }
        $objs += $obj
    }
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:DriverSigTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_driver_sig.exe"
        & $clang -o $script:DriverSigTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "driver signature host test link failed" }
    }
}

function Invoke-RoboTestDriverSig {
    Invoke-RoboBuildDriverSigTest
    Write-Step "TEST" "test_driver_sig.exe"
    & $script:DriverSigTestOut
    if ($LASTEXITCODE -ne 0) { throw "driver signature host tests failed" }
}

function Invoke-RoboBuildImage {
    param([string]$Flags = "")
    Invoke-RoboBuildKernel
    if (-not (Test-Path $script:EfiOut)) { throw "BOOTX64.EFI missing - build efi first" }
    $py = Get-Python
    $manifest = Join-Path $script:Boot "config\roboboot.manifest"
    $mkesp = Join-Path $script:Boot "tools\make_esp.py"
    $kern = Join-Path $script:Boot "volumes\sys\kernel.elf"
    $initrd = Join-Path $script:Boot "volumes\sys\initrd"
    $deps = @($script:EfiOut, $manifest, $mkesp, $kern, $initrd)
    $wantFlags = Get-EspFlagKey $Flags
    $stale = $true
    if (-not $script:RoboRebuild -and (Test-Path $script:EspImg) -and (Test-Path $script:EspFlagFile)) {
        $imgTime = (Get-Item $script:EspImg).LastWriteTimeUtc
        $stale = $false
        foreach ($d in $deps) {
            if ((Test-Path $d) -and (Get-Item $d).LastWriteTimeUtc -gt $imgTime) { $stale = $true }
        }
        $haveFlags = [IO.File]::ReadAllText($script:EspFlagFile).Trim()
        if (-not $stale -and $haveFlags -eq $wantFlags) { return }
    }
    $imgArgs = @(
        (Join-Path $script:Boot "tools\make_esp.py"),
        "--bootloader", $script:Boot,
        "--efi", $script:EfiOut,
        "--out", $script:EspImg
    )
    if ($wantFlags) { $imgArgs += @("--flags", $wantFlags) }
    Write-Step "IMAGE" "build\roboboot-esp.img"
    & $py @imgArgs
    if ($LASTEXITCODE -ne 0) { throw "ESP image failed" }
    [IO.File]::WriteAllText($script:EspFlagFile, $wantFlags)
}

function Write-PaddedEnv([string]$Src, [string]$Dst) {
    $text = "ROBOENV1`ndebug=0`n"
    if (Test-Path $Dst) {
        $have = [IO.File]::ReadAllText($Dst)
        if ($have.StartsWith("ROBOENV1")) { $text = $have }
    } elseif (Test-Path $Src) {
        $have = [IO.File]::ReadAllText($Src)
        if ($have.StartsWith("ROBOENV1")) { $text = $have }
    }
    $text = $text -replace "`0", "" -replace "`r`n", "`n" -replace "`r", "`n"
    if ($text -notmatch '(?m)^debug=') { $text = $text.TrimEnd("`n") + "`ndebug=0`n" }
    $block = New-Object byte[] 4096
    for ($i = 0; $i -lt 4096; $i++) { $block[$i] = 35 }
    $payload = [Text.Encoding]::ASCII.GetBytes($text)
    $n = [Math]::Min($payload.Length, 4095)
    [Array]::Copy($payload, $block, $n)
    New-Item -ItemType Directory -Force -Path (Split-Path $Dst) | Out-Null
    [IO.File]::WriteAllBytes($Dst, $block)
}

function Copy-EspTree([string]$SrcDir, [string]$DstDir) {
    if (-not (Test-Path $SrcDir)) { return }
    $skipExt = @(".png", ".svg", ".gitkeep", ".gitignore")
    $skipDir = @("icons", "build", "src", "include", "tests", "tools")
    New-Item -ItemType Directory -Force -Path $DstDir | Out-Null
    Get-ChildItem $SrcDir -Force | ForEach-Object {
        if ($_.Name.StartsWith(".")) { return }
        if ($_.PSIsContainer) {
            if ($skipDir -contains $_.Name) { return }
            Copy-EspTree $_.FullName (Join-Path $DstDir $_.Name)
            return
        }
        if ($skipExt -contains $_.Extension.ToLowerInvariant()) { return }
        if ($_.Name -eq "qemu.flags") { return }
        if ($_.Name -eq "roboboot.env") {
            Write-PaddedEnv $_.FullName (Join-Path $DstDir $_.Name)
            return
        }
        Copy-Item $_.FullName (Join-Path $DstDir $_.Name) -Force
    }
}

function Publish-RoboDist {
    New-Item -ItemType Directory -Force -Path $script:Dist | Out-Null
    foreach ($junk in @("BOOTX64.EFI", "kernel.elf", "roboboot.exe", "roboboot-esp.img", "COMBINED.txt")) {
        $p = Join-Path $script:Dist $junk
        if (Test-Path $p) { Remove-Item $p -Force }
    }

    $efiBoot = Join-Path $script:Dist "EFI\BOOT\BOOTX64.EFI"
    $efiVendor = Join-Path $script:Dist "EFI\Roboticela\BOOTX64.EFI"
    New-Item -ItemType Directory -Force -Path (Split-Path $efiBoot) | Out-Null
    New-Item -ItemType Directory -Force -Path (Split-Path $efiVendor) | Out-Null
    if (-not (Test-Path $script:EfiOut)) { throw "combine missing BOOTX64.EFI" }
    Copy-Item $script:EfiOut $efiBoot -Force
    Copy-Item $script:EfiOut $efiVendor -Force

    Copy-EspTree (Join-Path $script:Boot "config") (Join-Path $script:Dist "config")
    Copy-EspTree (Join-Path $script:Boot "assets") (Join-Path $script:Dist "assets")
    Copy-EspTree (Join-Path $script:Boot "volumes") (Join-Path $script:Dist "volumes")

    $kern = Join-Path $script:Dist "volumes\sys\kernel.elf"
    if (-not (Test-Path $kern) -and (Test-Path $script:KernelOut)) {
        New-Item -ItemType Directory -Force -Path (Split-Path $kern) | Out-Null
        Copy-Item $script:KernelOut $kern -Force
    }
    if (-not (Test-Path $efiBoot)) { throw "combine missing EFI\BOOT\BOOTX64.EFI" }
    if (-not (Test-Path $kern)) { throw "combine missing volumes\sys\kernel.elf" }

    $note = @(
        "Copy the contents of this folder onto a FAT32 USB drive.",
        "The stick root must contain EFI\BOOT\BOOTX64.EFI - do not nest an extra folder.",
        "Then boot the PC from USB.",
        "",
        "EFI\BOOT\BOOTX64.EFI        default firmware boot path",
        "EFI\Roboticela\BOOTX64.EFI  vendor boot path",
        "config\                     catalog and saved settings",
        "assets\                     splash, pictures, fonts",
        "volumes\sys\kernel.elf      kernel"
    )
    [IO.File]::WriteAllLines((Join-Path $script:Dist "USB.txt"), $note)
    Write-Step "USB" "EFI\BOOT\BOOTX64.EFI"
}

function Invoke-RoboCombine {
    param([string]$Flags = "")
    Write-Step "COMBINE" "kernel + bootloader"
    Write-Step "KERN" "individual kernel.elf"
    Invoke-RoboBuildKernel
    Write-Step "EFI" "individual BOOTX64.EFI"
    Invoke-RoboBuildEfi
    Write-Step "LINK" "pack kernel and bootloader into ESP"
    Invoke-RoboBuildImage -Flags $Flags
    Publish-RoboDist
}

function Invoke-RoboTestHost {
    Invoke-RoboBuildTest
    Write-Step "TEST" "roboboot-test.exe"
    & $script:TestOut $script:Boot
    if ($LASTEXITCODE -ne 0) { throw "host tests failed" }
    Invoke-RoboTestPmm
    Invoke-RoboTestVmm
    Invoke-RoboTestTime
    Invoke-RoboTestSchedQueue
    Invoke-RoboTestThreadState
    Invoke-RoboTestProcessState
    Invoke-RoboTestProcessHost
    Invoke-RoboTestContextLayout
    Invoke-RoboTestElf
    Invoke-RoboTestRfs
    Invoke-RoboTestDriverSig
}

function Invoke-RoboClean {
    if (Test-Path $script:Build) {
        Write-Step "CLEAN" "bootloader\build"
        Remove-Item $script:Build -Recurse -Force
    }
    $kbuild = Join-Path $script:KernelRoot "build"
    if (Test-Path $kbuild) {
        Write-Step "CLEAN" "kernel\build"
        Remove-Item $kbuild -Recurse -Force
    }
    if (Test-Path $script:Dist) {
        Write-Step "CLEAN" "build"
        Remove-Item $script:Dist -Recurse -Force
    }
}

function Invoke-RoboDistclean {
    Invoke-RoboClean
    $baked = @()
    $baked += Get-ChildItem (Join-Path $script:Boot "assets") -Filter *.rbim -File -ErrorAction SilentlyContinue
    $baked += Get-ChildItem (Join-Path $script:Boot "assets\fonts") -Filter *.rbfn -File -ErrorAction SilentlyContinue
    foreach ($p in @(
        (Join-Path $script:Boot "volumes\sys\kernel.elf"),
        (Join-Path $script:Boot "volumes\sys\recovery.elf"),
        (Join-Path $script:Boot "volumes\sys\install.elf"),
        (Join-Path $script:Boot "volumes\sys\initrd"),
        (Join-Path $script:Boot "volumes\windows\EFI\Microsoft\Boot\bootmgfw.efi")
    )) {
        if (Test-Path $p) { $baked += Get-Item $p }
    }
    foreach ($f in $baked) {
        Remove-Item $f.FullName -Force
    }
    if ($baked.Count -gt 0) { Write-Step "CLEAN" "baked assets and dummy volumes" }
}

# Names used by the existing QEMU scripts.
function Invoke-BuildEfi {
    Invoke-RoboBuildEfi
}

function Invoke-MakeEsp {
    param([string]$Flags = "")
    Invoke-RoboBuildImage -Flags $Flags
}
