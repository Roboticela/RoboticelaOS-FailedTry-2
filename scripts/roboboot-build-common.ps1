# RoboBoot / RoboticelaOS build engine.
# Shared by configure, build, host run, tests, and QEMU scripts.

$script:Repo = Split-Path -Parent $PSScriptRoot
$script:Boot = Join-Path $script:Repo "Bootloader"
$script:Build = Join-Path $script:Repo "Build\Bootloader"
$script:Dist = Join-Path $script:Repo "Build\dist"
$script:KernelRoot = Join-Path $script:Repo "Kernel"
$script:KernelOut = Join-Path $script:Repo "Build\Kernel\kernel.elf"
$script:PmmTestOut = Join-Path $script:Repo "Build\Kernel\test_pmm.exe"
$script:VmmTestOut = Join-Path $script:Repo "Build\Kernel\test_vmm.exe"
$script:TimeTestOut = Join-Path $script:Repo "Build\Kernel\test_time.exe"
$script:SchedQueueTestOut = Join-Path $script:Repo "Build\Kernel\test_sched_queue.exe"
$script:ThreadStateTestOut = Join-Path $script:Repo "Build\Kernel\test_thread_state.exe"
$script:ProcessStateTestOut = Join-Path $script:Repo "Build\Kernel\test_process_state.exe"
$script:ProcessHostTestOut = Join-Path $script:Repo "Build\Kernel\test_process_host.exe"
$script:ContextLayoutTestOut = Join-Path $script:Repo "Build\Kernel\test_context_layout.exe"
$script:ElfTestOut = Join-Path $script:Repo "Build\Kernel\test_elf.exe"
$script:RfsTestOut = Join-Path $script:Repo "Build\Kernel\test_rfs.exe"
$script:DriverSigTestOut = Join-Path $script:Repo "Build\Kernel\test_driver_sig.exe"
$script:CompositorTestOut = Join-Path $script:Repo "Build\Kernel\test_compositor.exe"
$script:NetTestOut = Join-Path $script:Repo "Build\Kernel\test_net.exe"
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

. (Join-Path $PSScriptRoot "Utilities\RoboBuildProgress.ps1")

function Get-RoboSources([string]$Kind) {
    $core = [string[]]@(
        "Source\rb_util.c",
        "Source\rb_crypto.c",
        "Source\rb_gfx.c",
        "Source\rb_image.c",
        "Source\rb_manifest.c",
        "Source\rb_env.c",
        "Source\rb_probe.c",
        "Source\rb_loader.c",
        "Source\rb_policy.c",
        "Source\rb_ui.c",
        "Source\rb_session.c"
    )
    $extra = @{
        host = [string[]]@("Source\host\main_host.c")
        test = [string[]]@("Tests\Bootloader\test_roboboot.c")
        efi  = [string[]]@("Source\efi\efi_crt.c", "Source\efi\efi_plat.c", "Source\efi\efi_bootmgr.c", "Source\efi\efi_kexec.c", "Source\efi\main_efi.c")
    }
    if (-not $extra.ContainsKey($Kind)) { throw "unknown source set: $Kind" }
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($c in $core) { [void]$out.Add($c) }
    foreach ($e in $extra[$Kind]) { [void]$out.Add($e) }
    if ($out.Count -lt 12) { throw "source list for $Kind is too short ($($out.Count))" }
    return $out
}

function Resolve-RoboBootSource([string]$Rel) {
    if ([IO.Path]::IsPathRooted($Rel)) { return $Rel }
    if ($Rel.StartsWith("Tests\") -or $Rel.StartsWith("Tests/")) {
        return (Join-Path $script:Repo $Rel)
    }
    return (Join-Path $script:Boot $Rel)
}

function Get-RoboVersion {
    $h = Join-Path $script:Boot "Include\roboboot.h"
    if (Test-Path $h) {
        $m = Select-String -Path $h -Pattern '#define\s+RbVersionStr\s+"([^"]+)"' | Select-Object -First 1
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

function Get-LlvmAr {
    $ar = Get-Command llvm-ar -ErrorAction SilentlyContinue
    if ($ar) { return $ar.Source }
    $dir = Split-Path (Get-Clang)
    foreach ($name in @("llvm-ar.exe", "llvm-ar")) {
        $hint = Join-Path $dir $name
        if (Test-Path $hint) { return $hint }
    }
    throw "llvm-ar not found (needed to archive libc.a)"
}

function Get-LlvmReadobj {
    $ro = Get-Command llvm-readobj -ErrorAction SilentlyContinue
    if ($ro) { return $ro.Source }
    $dir = Split-Path (Get-Clang)
    foreach ($name in @("llvm-readobj.exe", "llvm-readobj")) {
        $hint = Join-Path $dir $name
        if (Test-Path $hint) { return $hint }
    }
    return $null
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

function Get-HeaderTreeDeps([string[]]$Dirs) {
    <#
      The recursive variant of `Get-HeaderDeps`, for the runtime's own tree.

      `Get-HeaderDeps` reads one directory level, and every root it is given is an *include root* whose
      subdirectories are named explicitly (`robosys`, `robowindow`, `robocompositor`). That is right for
      the libc SDK, whose layout is stable, and wrong for the runtime: `Runtime/Include` is a tree of
      per-module directories and `Runtime/Source` holds the *internal* headers the module sources share
      (`RoboDesktopInternal.h`). Neither was a compile input as far as this build was concerned, so a
      struct that grew a field in an internal header left every object that had been compiled against the
      old layout in place -- and the guest then linked new and old objects together and reported a desktop
      with no windows, because one module wrote `windowCount` where another read the field before it.

      A mismatched object is not a build error anybody sees, which is why this is worth the extra `stat`:
      the failure surfaces as a wrong number on a target that costs minutes to boot.
    #>
    $h = @()
    foreach ($d in $Dirs) {
        if (Test-Path $d) {
            $h += @(Get-ChildItem $d -Filter *.h -File -Recurse | ForEach-Object { $_.FullName })
        }
    }
    return $h
}

function Test-Stale([string]$Source, [string]$Object, [string[]]$Deps) {
    if (-not (Test-Path -LiteralPath $Source)) {
        throw ("missing source: " + $Source)
    }
    if (-not (Test-Path -LiteralPath $Object)) { return $true }
    $objTime = (Get-Item -LiteralPath $Object).LastWriteTimeUtc
    if ((Get-Item -LiteralPath $Source).LastWriteTimeUtc -gt $objTime) { return $true }
    foreach ($d in $Deps) {
        if ($d -and (Test-Path -LiteralPath $d) -and
            (Get-Item -LiteralPath $d).LastWriteTimeUtc -gt $objTime) {
            return $true
        }
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
    $force = [bool]$script:RoboRebuild
    $stale = $force
    if (-not $stale) {
        # Prefer clang -MMD deps when present; otherwise use the caller's header list.
        $depFile = [System.IO.Path]::ChangeExtension($Object, ".d")
        if ((Test-Path -LiteralPath $depFile) -and (Get-RoboMakefileDeps $depFile).Count -gt 0) {
            $stale = Test-RoboObjectStale -Src $Source -Obj $Object -Force:$false
        } else {
            $stale = Test-Stale $Source $Object $Deps
        }
    }
    if (-not $stale) {
        Write-RoboBuildProgress -Skipped -Src $Source
        return $false
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $Object) | Out-Null
    $depOut = [System.IO.Path]::ChangeExtension($Object, ".d")
    if (-not $script:RoboBuildProgress) {
        Write-Step "CC" (Resolve-BootRel $Source)
    }
    & $Clang @CFlags -MMD -MP -MF $depOut -c $Source -o $Object
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $(Resolve-BootRel $Source)" }
    Write-RoboBuildProgress -Compiled -Src $Source
    return $true
}

function Resolve-CcBatchObject {
    param(
        [string]$Source,
        [int]$Index,
        [scriptblock]$ObjectFor,
        [string[]]$Objects
    )
    if ($Objects -and $Objects.Count -gt 0) {
        if ($Index -ge $Objects.Count) {
            throw ("Invoke-CcBatch Objects shorter than Sources at index " + $Index)
        }
        return $Objects[$Index]
    }
    if (-not $ObjectFor) {
        throw "Invoke-CcBatch requires -ObjectFor or -Objects"
    }
    return (& $ObjectFor $Source)
}

function Invoke-CcBatch {
    param(
        [string]$Label,
        [string]$Clang,
        [string[]]$CFlags,
        [string[]]$Sources,
        [scriptblock]$ObjectFor,
        [string[]]$Objects,
        [string[]]$Deps
    )
    if (-not $Sources -or $Sources.Count -eq 0) {
        return @{ Objects = @(); Compiled = $false }
    }
    $stale = 0
    $i = 0
    foreach ($src in $Sources) {
        $obj = Resolve-CcBatchObject -Source $src -Index $i -ObjectFor $ObjectFor -Objects $Objects
        $i++
        $force = [bool]$script:RoboRebuild
        $need = $force
        if (-not $need) {
            $depFile = [System.IO.Path]::ChangeExtension($obj, ".d")
            if ((Test-Path -LiteralPath $depFile) -and (Get-RoboMakefileDeps $depFile).Count -gt 0) {
                $need = Test-RoboObjectStale -Src $src -Obj $obj -Force:$false
            } else {
                $need = Test-Stale $src $obj $Deps
            }
        }
        if ($need) { $stale++ }
    }
    Initialize-RoboBuildProgress -Total $Sources.Count -ToCompile $stale -Cached ($Sources.Count - $stale) -Label $Label
    $compiled = $false
    $objs = @()
    $i = 0
    foreach ($src in $Sources) {
        $obj = Resolve-CcBatchObject -Source $src -Index $i -ObjectFor $ObjectFor -Objects $Objects
        $i++
        if (Invoke-Cc -Clang $Clang -CFlags $CFlags -Source $src -Object $obj -Deps $Deps) {
            $compiled = $true
        }
        $objs += $obj
    }
    Complete-RoboBuildProgress
    return @{ Objects = $objs; Compiled = $compiled }
}

function Invoke-CcDirBatch {
    <#
      Chromium/ninja-style batch: one PLAN line for a directory of objects named
      from each source's basename. Prefer this over a bare Invoke-Cc loop.
    #>
    param(
        [string]$Label,
        [string]$Clang,
        [string[]]$CFlags,
        [string[]]$Sources,
        [string]$ObjDir,
        [string]$Ext = ".obj",
        [string[]]$Deps
    )
    New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null
    $objects = @($Sources | ForEach-Object {
        Join-Path $ObjDir (([IO.Path]::GetFileNameWithoutExtension($_)) + $Ext)
    })
    return Invoke-CcBatch -Label $Label -Clang $Clang -CFlags $CFlags `
        -Sources $Sources -Objects $objects -Deps $Deps
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
    $scriptPath = Join-Path $script:Boot "Tools\bake_assets.py"
    $required = @(
        (Join-Path $script:Boot "Assets\logo.rbim"),
        (Join-Path $script:Boot "Assets\fonts\ui-title.rbfn"),
        (Join-Path $script:Boot "Assets\fonts\ui-body.rbfn"),
        (Join-Path $script:Boot "Assets\fonts\ui-caption.rbfn")
    )
    $inputs = @(
        $scriptPath,
        (Join-Path $script:Boot "Assets\logo.svg"),
        (Join-Path $script:Boot "Assets\HDD.png"),
        (Join-Path $script:Boot "Assets\SSD.png"),
        (Join-Path $script:Boot "Assets\NVMe.png"),
        (Join-Path $script:Boot "Assets\USB.png")
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
        "-I", (Join-Path $script:Boot "Include")
    )
    $deps = Get-HeaderDeps @((Join-Path $script:Boot "Include"))
    $sources = @(Get-RoboSources "host" | ForEach-Object { Resolve-RoboBootSource $_ })
    $batch = Invoke-CcBatch -Label "boot-host" -Clang $clang -CFlags $cflags -Sources $sources `
        -Deps $deps -ObjectFor {
            param($src)
            $rel = Resolve-BootRel $src
            Join-Path $script:ObjHost (([IO.Path]::GetFileNameWithoutExtension($rel)) + ".obj")
        }
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        "-I", (Join-Path $script:Boot "Include")
    )
    $deps = Get-HeaderDeps @((Join-Path $script:Boot "Include"))
    $sources = @()
    $objects = @()
    foreach ($rel in (Get-RoboSources "test")) {
        $sources += (Resolve-RoboBootSource $rel)
        $objects += (Join-Path $script:ObjTest (([IO.Path]::GetFileNameWithoutExtension($rel)) + ".obj"))
    }
    $batch = Invoke-CcBatch -Label "boot-test" -Clang $clang -CFlags $cflags `
        -Sources $sources -Objects $objects -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        "-I", (Join-Path $script:Boot "Include"),
        "-I", (Join-Path $script:Boot "Source\efi\libc"),
        "-I", (Join-Path $script:Boot "Source\efi")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $script:Boot "Include"),
        (Join-Path $script:Boot "Include\roboticela"),
        (Join-Path $script:Boot "Source\efi"),
        (Join-Path $script:Boot "Source\efi\libc")
    )
    $sources = @()
    $objects = @()
    foreach ($rel in (Get-RoboSources "efi")) {
        $sources += (Resolve-RoboBootSource $rel)
        $objects += (Join-Path $script:ObjEfi (([IO.Path]::GetFileNameWithoutExtension($rel)) + ".obj"))
    }
    $batch = Invoke-CcBatch -Label "boot-efi" -Clang $clang -CFlags $cflags `
        -Sources $sources -Objects $objects -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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

function Invoke-RoboBuildLibc {
    $clang = Get-Clang
    $ar = Get-LlvmAr
    $libc = Join-Path $script:Repo "Libc"
    $lbuild = Join-Path $script:Repo "Build\Libc"
    $lobj = Join-Path $lbuild "obj"
    New-Item -ItemType Directory -Force -Path $lobj | Out-Null
    $inc = Join-Path $libc "include"
    $sdk = Join-Path $script:Repo "Sdk\include"
    $cflags = @(
        "-target", "x86_64-unknown-none",
        "-std=c11", "-O2",
        "-ffreestanding", "-nostdlib", "-static",
        "-fno-pic", "-fno-pie", "-fno-stack-protector",
        "-fno-asynchronous-unwind-tables",
        "-mcmodel=large",
        "-mno-red-zone", "-fno-builtin",
        "-isystem", $inc,
        "-I", $inc,
        "-I", $sdk
    )
    $deps = Get-HeaderDeps @($inc, (Join-Path $inc "robo"), (Join-Path $inc "sys"), (Join-Path $sdk "robosys"))
    $srcs = @(
        "arch\x86_64\syscall.S",
        "src\errno\errno.c",
        "src\string\string.c",
        "src\memory\malloc.c",
        "src\memory\mmap.c",
        "src\stdlib\stdlib.c",
        "src\stdio\stdio.c",
        "src\time\time.c",
        "src\file\file.c",
        "src\process\process.c",
        "src\thread\thread.c",
        "src\socket\socket.c",
        "src\env\env.c",
        "src\internal\start.c"
    )
    $sources = @()
    $objects = @()
    $i = 0
    foreach ($rel in $srcs) {
        $sources += (Join-Path $libc $rel)
        $objects += (Join-Path $lobj ("libc_" + $i + ".o"))
        $i++
    }
    $batch = Invoke-CcBatch -Label "libc" -Clang $clang -CFlags $cflags `
        -Sources $sources -Objects $objects -Deps $deps
    $objs = @($batch.Objects)
    $liba = Join-Path $lbuild "libc.a"
    if ($script:RoboRebuild -or $batch.Compiled -or (Test-LinkStale $liba $objs)) {
        Write-Step "AR" "libc\build\libc.a"
        if (Test-Path $liba) { Remove-Item $liba -Force }
        & $ar "rcs" $liba @objs
        if ($LASTEXITCODE -ne 0) { throw "libc.a archive failed" }
    }
    $crtPairs = @(
        @{ Name = "crt0"; Rel = "arch\x86_64\crt0.S" },
        @{ Name = "crti"; Rel = "crt\crti.S" },
        @{ Name = "crtn"; Rel = "crt\crtn.S" }
    )
    $crtSources = @($crtPairs | ForEach-Object { Join-Path $libc $_.Rel })
    $crtObjects = @($crtPairs | ForEach-Object { Join-Path $lobj ($_.Name + ".o") })
    $null = Invoke-CcBatch -Label "libc-crt" -Clang $clang -CFlags $cflags `
        -Sources $crtSources -Objects $crtObjects -Deps $deps
    $crt = @{}
    for ($ci = 0; $ci -lt $crtPairs.Count; $ci++) {
        $crt[$crtPairs[$ci].Name] = $crtObjects[$ci]
    }
    return @{ Lib = $liba; Crt = $crt; Inc = $inc; Sdk = $sdk; Deps = $deps }
}

function Test-RoboNoHostLibc([string]$Elf) {
    $ro = Get-LlvmReadobj
    if (-not $ro) { return }
    $out = & $ro "--needed-libs" $Elf 2>&1 | Out-String
    if ($out -match "libc\.so" -or $out -match "ld-linux" -or $out -match "libgcc_s") {
        throw ("host libc leakage in " + $Elf + "`n" + $out)
    }
}

function Get-RoboRuntimeRoot {
    return (Join-Path $script:Repo "Runtime")
}

function Assert-RoboRuntimeEngines {
    $rt = Get-RoboRuntimeRoot
    # Independent Roboticela Runtime (Phases 0-6+) - required for host/guest wiring.
    $required = @(
        @{ Name = "RoboticelaRuntime.h"; Path = (Join-Path $rt "Include\RoboticelaRuntime.h") },
        @{ Name = "RoboBase"; Path = (Join-Path $rt "Include\RoboBase\RoboError.h") },
        @{ Name = "RoboMem"; Path = (Join-Path $rt "Include\RoboMem\RoboMem.h") },
        @{ Name = "RoboGraphics"; Path = (Join-Path $rt "Include\RoboGraphics\RoboGraphics.h") },
        @{ Name = "RoboFont"; Path = (Join-Path $rt "Include\RoboFont\RoboFont.h") },
        @{ Name = "Build.ps1"; Path = (Join-Path $rt "Tools\Build.ps1") }
    )
    $missing = @()
    foreach ($r in $required) {
        if (-not (Test-Path -LiteralPath $r.Path)) { $missing += $r.Name }
    }
    if ($missing.Count -gt 0) {
        throw ("Roboticela Runtime incomplete (missing: " + ($missing -join ', ') +
            "). Expected independent runtime under runtime/Include + runtime/Source.")
    }
    Write-Step "RUNTIME" "independent Roboticela Runtime present"
}

function New-RoboRuntimeManifest {
    param([string]$OutFile)
    $rt = Get-RoboRuntimeRoot
    function Count-Files([string]$dir, [string]$filter) {
        if (-not (Test-Path -LiteralPath $dir)) { return 0 }
        return @(Get-ChildItem -LiteralPath $dir -Filter $filter -Recurse -File -ErrorAction SilentlyContinue).Count
    }
    function Eng-Line([string]$id, [string]$rel) {
        $path = Join-Path $rt $rel
        $present = if (Test-Path -LiteralPath $path) { "present" } else { "missing" }
        return "engine.$id.status=$present`nengine.$id.path=$rel"
    }
    $gfxVer = "unknown"
    $gfxHdr = Join-Path $rt "Include\RoboGraphics\RoboGraphicsVersion.h"
    if (Test-Path -LiteralPath $gfxHdr) {
        foreach ($line in Get-Content -LiteralPath $gfxHdr) {
            if ($line -match 'RoboGraphicsVersionString\s+"([^"]+)"') {
                $gfxVer = $Matches[1]
                break
            }
        }
    }
    $rtVer = "unknown"
    $rtHdr = Join-Path $rt "Include\RoboticelaRuntime.h"
    if (Test-Path -LiteralPath $rtHdr) {
        foreach ($line in Get-Content -LiteralPath $rtHdr) {
            if ($line -match 'ROBOTICELA_RUNTIME_VERSION_STRING\s+"([^"]+)"') {
                $rtVer = $Matches[1]
                break
            }
        }
    }
    $lines = @(
        "# RoboticelaOS Runtime Manifest (independent runtime)",
        "version=2",
        "generated=$(Get-Date -Format 'yyyy-MM-ddTHH:mm:ss')",
        "runtime.kind=independent",
        "runtime.version=$rtVer",
        "runtime.graphics.version=$gfxVer",
        "runtime.include=Include",
        "runtime.source=Source",
        "guest.bins=robo-runtime",
        "guest.apis=robobase,robomem,robographics,robofont,roboi18n,robounicode",
        "boot.services=compositor,window-server",
        "headers.c=$(Count-Files (Join-Path $rt 'Include') '*.h')",
        "sources.c=$(Count-Files (Join-Path $rt 'Source') '*.c')",
        (Eng-Line "robographics" "Include\RoboGraphics"),
        (Eng-Line "robofont" "Include\RoboFont"),
        (Eng-Line "roboi18n" "Include\RoboI18N"),
        (Eng-Line "robounicode" "Include\RoboUnicode"),
        (Eng-Line "chromium_ref_optional" "robospark\v8")
    )
    $dir = Split-Path $OutFile -Parent
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    [IO.File]::WriteAllText($OutFile, ($lines -join "`n") + "`n")
    Write-Step "RUNTIME" "engines.manifest"
}

function Invoke-RoboBuildRuntime {
    Assert-RoboRuntimeEngines
    Write-Step "RUNTIME" "build guest runtime ELFs via userland"
    Invoke-RoboBuildKernel  # includes userland + initrd pack with runtime
}

function Invoke-RoboBuildUserland {
    Assert-RoboRuntimeEngines
    $kroot = $script:KernelRoot
    $user = Join-Path $script:Repo "user"
    $runtime = Get-RoboRuntimeRoot
    $ubuild = Join-Path $kroot "build\user"
    New-Item -ItemType Directory -Force -Path $ubuild | Out-Null
    $manifest = Join-Path $ubuild "engines.manifest"
    New-RoboRuntimeManifest -OutFile $manifest
    $clang = Get-Clang
    $ld = Get-LdLld
    $ldscript = Join-Path $user "linker.ld"
    $libc = Invoke-RoboBuildLibc
    $cflags = @(
        "-target", "x86_64-unknown-none",
        "-std=c11", "-O2",
        "-ffreestanding", "-nostdlib", "-static",
        "-fno-pic", "-fno-pie", "-fno-stack-protector",
        "-fno-asynchronous-unwind-tables",
        "-mcmodel=large",
        "-mno-red-zone", "-fno-builtin",
        "-isystem", $libc.Inc,
        "-I", $libc.Inc,
        "-I", $libc.Sdk,
        "-I", $user,
        "-I", (Join-Path $runtime "Include")
    )
    $sdkdeps = Get-HeaderDeps @(
        $libc.Inc,
        (Join-Path $libc.Sdk "robosys"),
        (Join-Path $libc.Sdk "robowindow"),
        (Join-Path $libc.Sdk "robocompositor"),
        (Join-Path $runtime "Include")
    )
    # The runtime's own headers, public *and* internal, as dependencies of the runtime sources alone. Kept
    # out of `$sdkdeps` on purpose: a guest program that does not include the runtime at all (`hello`,
    # `systest`, the shims) should not be recompiled because a module header moved. See
    # `Get-HeaderTreeDeps` for what leaving them out cost.
    $runtimeDeps = Get-HeaderTreeDeps @(
        (Join-Path $runtime "Include"),
        (Join-Path $runtime "Source")
    )
    $progs = @(
        @{ Name = "hello"; Srcs = @("hello\hello.c"); Libc = $true },
        @{ Name = "fault"; Srcs = @("fault\start.S"); Libc = $false },
        @{ Name = "systest"; Srcs = @("systest\systest.c", "systest\regs.S", "systest\worker.S"); Libc = $true },
        @{ Name = "a"; Srcs = @("iso\a.c"); Libc = $true },
        @{ Name = "b"; Srcs = @("iso\b.c"); Libc = $true },
        @{ Name = "fd-test"; Srcs = @("fd-test\fd-test.c"); Libc = $true },
        @{ Name = "robofs-test"; Srcs = @("robofs-test\robofs-test.c"); Libc = $true },
        @{ Name = "robo"; Srcs = @("robo\main.c", "lib\toolchain.c"); Libc = $true },
        @{ Name = "terminal"; Srcs = @("terminal\main.c"); Libc = $true },
        @{ Name = "clang-shim"; Srcs = @("clang\shim.c", "lib\toolchain.c"); Libc = $true },
        @{ Name = "clangxx-shim"; Srcs = @("clang\shim.c", "lib\toolchain.c"); Libc = $true },
        @{ Name = "clang-rt"; Srcs = @("clang\runtime.c"); Libc = $true },
        @{ Name = "init"; Srcs = @("init\main.c", "init\service.c", "init\config.c"); Libc = $true },
        @{ Name = "test-service"; Srcs = @("services\test-service\test-service.c"); Libc = $true },
        @{ Name = "crash-test"; Srcs = @("services\crash-test\start.S"); Libc = $false },
        @{ Name = "isol-test"; Srcs = @("services\isol-test\start.S"); Libc = $false },
        @{ Name = "window-server"; Srcs = @(
            "services\window-server\main.c",
            "services\window-server\server.c",
            "services\window-server\compositor.c"
        ); Libc = $true },
        @{ Name = "compositor"; Srcs = @(
            "lib\compose.c",
            "services\compositor\main.c",
            "services\compositor\server.c",
            "services\compositor\output.c"
        ); Libc = $true },
        @{ Name = "window-test"; Srcs = @("lib\window.c", "window-test\main.c"); Libc = $true },
        @{ Name = "window-peer"; Srcs = @("lib\window.c", "window-peer\main.c"); Libc = $true },
        @{ Name = "window-child"; Srcs = @("lib\window.c", "window-child\main.c"); Libc = $true },
        @{ Name = "compositor-test"; Srcs = @(
            "lib\window.c", "lib\compose.c", "compositor-test\main.c"
        ); Libc = $true },
        @{ Name = "nettest"; Srcs = @(
            "lib\dns.c", "lib\http.c", "lib\tls.c", "nettest\main.c"
        ); Libc = $true },
        @{ Name = "libc-test"; Srcs = @("libc-test\main.c"); Libc = $true },
        @{ Name = "argv-test"; Srcs = @("argv-test\main.c"); Libc = $true },
        @{ Name = "thread-test"; Srcs = @("thread-test\main.c"); Libc = $true },
        @{ Name = "file-test"; Srcs = @("file-test\main.c"); Libc = $true },
        @{ Name = "network-test"; Srcs = @("network-test\main.c"); Libc = $true },
        # The guest runtime: RoboticelaOS's own desktop program, and the only program this build links
        # that is not a service or a test. It compiles **every** source of each module the desktop pipeline
        # touches, discovered from the module's own directory rather than re-typed here -- the same
        # rule `Tools/Build.ps1` states file by file in `$ProdSrcs`.
        #
        # Discovered rather than copied because a copied list drifts, and this one did: it went on
        # compiling the deleted RoboDemo layer, so the OS booted to a throwaway demo page for two phases
        # after the host had a real shell. The next module added to the runtime now reaches the guest image
        # by being a source file in a module this list names.
        @{ Name = "robo-runtime"; Srcs = @("robo-runtime\main.c"); Libc = $true;
          ExtraCFlags = @("-DRoboWindowHasQemu=1");
          RuntimeModules = @(
            # The runtime's own floor: errors, memory, the containers CSS and HTML are built out of,
            # and the Unicode tables everything that measures text reads.
            "RoboBase",
            "RoboMem",
            "RoboContainers",
            "RoboUnicode",
            # Locale and language: RoboFont looks a face's names up through RoboI18N's locale model, so a
            # guest that draws glyphs needs the module even though nothing here is localised yet.
            "RoboI18N",
            # Markup, all the way to pictures: the modules whose output the shell's window is made of.
            # RoboDesktop's document bridge is here because a window's contents *are* a real document.
            "RoboHTML",
            "RoboCSS",
            "RoboFont",
            "RoboLayout",
            "RoboPaint",
            "RoboRender",
            # And the surface those pictures land on: pixels, the display, the window, the shell.
            "RoboGraphics",
            "RoboDisplay",
            "RoboWindow",
            # The typography layer the shell asks for its faces by *role* (Phase 12.3). It sits directly
            # under RoboDesktop -- the shell resolves roles and never names a font file -- and over
            # RoboFont, whose pack it resolves against.
            "RoboTypography",
            # The text layer (Phase 12.4): the module that turns a string into runs, glyphs and a
            # measurement. It is named here because the guest's *measurer* (RoboLayout) and its *painter*
            # (RoboPaint) both consume it -- the guest resolves no glyphs of its own, and the old
            # cmap-per-code-point loops they used to carry are gone. A guest built without it links no
            # text at all, which is exactly what this list existing to be exhaustive prevents.
            "RoboText",
            "RoboCompositor",
            "RoboDesktop"
          );
        },
        # Legacy Chromium-engine demos — optional; skipped when sources absent.
        @{ Name = "robospark"; Root = $runtime; Optional = $true; Srcs = @(
            "robocore\src\robocore.c",
            "robospark\src\robospark.c",
            "robospark\cli\main.c"
        ); Libc = $true },
        @{ Name = "robolayer-demo"; Root = $runtime; Optional = $true; Srcs = @(
            "robocore\src\robocore.c",
            "robolayer\src\robolayer.c",
            "robolayer\demo\main.c"
        ); Libc = $true },
        @{ Name = "robosail"; Root = $runtime; Optional = $true; Srcs = @(
            "robocore\src\robocore.c",
            "robospark\src\robospark.c",
            "robolayer\src\robolayer.c",
            "robobridge\src\robobridge.c",
            "roboaqua\src\roboaqua.c",
            "robokit\src\robokit.c",
            "robosail\src\main.c"
        ); ExtraUserSrcs = @("lib\window.c"); Libc = $true }
    )
    $elfs = @{}
    foreach ($p in $progs) {
        $srcRoot = if ($p.ContainsKey("Root") -and $p.Root) { $p.Root } else { $user }
        $optional = ($p.ContainsKey("Optional") -and $p.Optional)
        $missing = @()
        foreach ($rel in $p.Srcs) {
            $cand = Join-Path $srcRoot $rel
            if (-not (Test-Path -LiteralPath $cand)) { $missing += $cand }
        }
        if ($missing.Count -gt 0) {
            if ($optional) {
                continue
            }
            throw ("missing sources for guest " + $p.Name + ": " + ($missing -join ', '))
        }
        $compileSources = [System.Collections.Generic.List[string]]::new()
        $compileObjects = [System.Collections.Generic.List[string]]::new()
        $i = 0
        $progFlags = $cflags
        if ($p.ContainsKey("ExtraCFlags") -and $p.ExtraCFlags) {
            $progFlags = $cflags + $p.ExtraCFlags
        }
        foreach ($rel in $p.Srcs) {
            $compileSources.Add((Join-Path $srcRoot $rel)) | Out-Null
            $compileObjects.Add((Join-Path $ubuild ($p.Name + "_" + $i + ".o"))) | Out-Null
            $i++
        }
        if ($p.ContainsKey("ExtraUserSrcs") -and $p.ExtraUserSrcs) {
            foreach ($rel in $p.ExtraUserSrcs) {
                $compileSources.Add((Join-Path $user $rel)) | Out-Null
                $compileObjects.Add((Join-Path $ubuild ($p.Name + "_ux_" + $i + ".o"))) | Out-Null
                $i++
            }
        }
        # The runtime sources this guest compiles, worked out rather than listed: every `.c` under each
        # named module, subdirectories included, with two kinds of directory left out because a guest
        # cannot call what is in them -- `tests`, which are the host suites, and the platform backends of
        # the *other* platforms (`win32`, `host`; the guest's own is `platform\qemu`, which the walk keeps).
        # What is left is exactly what `Tools/Build.ps1`'s `$ProdSrcs` names for those modules, without this
        # file having to repeat the list and drift from it.
        $runtimeSrcs = @()
        if ($p.ContainsKey("RuntimeSrcs") -and $p.RuntimeSrcs) { $runtimeSrcs += $p.RuntimeSrcs }
        if ($p.ContainsKey("RuntimeModules") -and $p.RuntimeModules) {
            foreach ($mod in $p.RuntimeModules) {
                $modRoot = Join-Path (Join-Path $runtime "Source") $mod
                if (-not (Test-Path -LiteralPath $modRoot)) {
                    throw ("missing runtime module for guest " + $p.Name + ": " + $modRoot)
                }
                $files = @(Get-ChildItem -LiteralPath $modRoot -Recurse -Filter "*.c" -File |
                    Where-Object { $_.FullName -notmatch '[\\/](tests?|win32|host)[\\/]' } |
                    Sort-Object -Property FullName)
                if ($files.Count -eq 0) {
                    throw ("runtime module " + $mod + " has no sources under " + $modRoot)
                }
                foreach ($f in $files) {
                    $runtimeSrcs += ("Source" + "\" + $mod + $f.FullName.Substring($modRoot.Length))
                }
            }
        }
        $progDeps = @($ldscript) + $sdkdeps
        if ($runtimeSrcs.Count -gt 0) {
            $progDeps = $progDeps + $runtimeDeps
        }
        foreach ($rel in $runtimeSrcs) {
            $src = Join-Path $runtime $rel
            if (-not (Test-Path -LiteralPath $src)) {
                throw ("missing runtime source for guest " + $p.Name + ": " + $src)
            }
            # Path-derived object names — index-based names collide when the source set grows.
            $stem = (($rel -replace '[\\/]', '_') -replace '\.c$', '')
            $compileSources.Add($src) | Out-Null
            $compileObjects.Add((Join-Path $ubuild ($p.Name + "_rt_" + $stem + ".o"))) | Out-Null
        }
        $batch = Invoke-CcBatch -Label ("user/" + $p.Name) -Clang $clang -CFlags $progFlags `
            -Sources @($compileSources) -Objects @($compileObjects) -Deps $progDeps
        $objs = @($batch.Objects)
        $elf = Join-Path $ubuild ($p.Name + ".elf")
        $linkobjs = $objs
        if ($p.Libc) {
            $linkobjs = @($libc.Crt["crti"], $libc.Crt["crt0"]) + $objs + @($libc.Lib, $libc.Crt["crtn"])
        }
        $stale = $script:RoboRebuild -or $batch.Compiled -or (Test-LinkStale $elf $linkobjs)
        if ($stale) {
            Write-Step "LD" ("user\" + $p.Name + ".elf")
            & $ld "-m" "elf_x86_64" "-static" "-no-pie" "-T" $ldscript "-o" $elf @linkobjs
            if ($LASTEXITCODE -ne 0) { throw ("user " + $p.Name + " link failed") }
        }
        if ($p.Libc) { Test-RoboNoHostLibc $elf }
        $elfs[$p.Name] = $elf
    }
    $py = Get-Python
    $pack = Join-Path $user "pack_robofs.py"
    $initrd = Join-Path $script:Boot "volumes\sys\initrd"
    New-Item -ItemType Directory -Force -Path (Split-Path $initrd) | Out-Null
    $txt = Join-Path $ubuild "test.txt"
    [IO.File]::WriteAllBytes($txt, [Text.Encoding]::ASCII.GetBytes("Roboticela OS"))
    $catalogIndex = Join-Path $ubuild "robo-catalog-index.txt"
    [IO.File]::WriteAllText($catalogIndex, "llvm 18.1.8`n")
    $defaults = Join-Path $ubuild "robo-toolchains-defaults.txt"
    [IO.File]::WriteAllText($defaults, "llvm=18.1.8`n")
    Write-Step "FS" "volumes\sys\initrd"
    $packArgs = [System.Collections.Generic.List[string]]::new()
    function Add-Pack([string]$Mount, [string]$Path) {
        if ($Path -and (Test-Path -LiteralPath $Path)) {
            $packArgs.Add($Mount + "=" + $Path) | Out-Null
        }
    }
    Add-Pack "/bin/hello" $elfs["hello"]
    Add-Pack "/bin/fault" $elfs["fault"]
    Add-Pack "/bin/systest" $elfs["systest"]
    Add-Pack "/bin/a" $elfs["a"]
    Add-Pack "/bin/b" $elfs["b"]
    Add-Pack "/bin/fd-test" $elfs["fd-test"]
    Add-Pack "/bin/robofs-test" $elfs["robofs-test"]
    Add-Pack "/bin/robo" $elfs["robo"]
    Add-Pack "/bin/clang" $elfs["clang-shim"]
    Add-Pack "/bin/clang++" $elfs["clangxx-shim"]
    Add-Pack "/System/init" $elfs["init"]
    Add-Pack "/System/services/test-service" $elfs["test-service"]
    Add-Pack "/System/services/crash-test" $elfs["crash-test"]
    Add-Pack "/System/services/isol-test" $elfs["isol-test"]
    Add-Pack "/System/services/window-server" $elfs["window-server"]
    Add-Pack "/System/services/compositor" $elfs["compositor"]
    Add-Pack "/System/robo/catalog/index" $catalogIndex
    Add-Pack "/System/robo/catalog/llvm/18.1.8/bin/clang" $elfs["clang-rt"]
    Add-Pack "/System/robo/catalog/llvm/18.1.8/bin/clang++" $elfs["clang-rt"]
    Add-Pack "/System/toolchains/defaults" $defaults
    Add-Pack "/System/toolchains/llvm/18.1.8/bin/clang" $elfs["clang-rt"]
    Add-Pack "/System/toolchains/llvm/18.1.8/bin/clang++" $elfs["clang-rt"]
    Add-Pack "/bin/window-test" $elfs["window-test"]
    Add-Pack "/bin/window-peer" $elfs["window-peer"]
    Add-Pack "/bin/window-child" $elfs["window-child"]
    Add-Pack "/bin/compositor-test" $elfs["compositor-test"]
    Add-Pack "/bin/nettest" $elfs["nettest"]
    Add-Pack "/bin/libc-test" $elfs["libc-test"]
    Add-Pack "/bin/argv-test" $elfs["argv-test"]
    Add-Pack "/bin/thread-test" $elfs["thread-test"]
    Add-Pack "/bin/file-test" $elfs["file-test"]
    Add-Pack "/bin/network-test" $elfs["network-test"]
    Add-Pack "/bin/robo-runtime" $elfs["robo-runtime"]
    Add-Pack "/System/runtime/robo-runtime" $elfs["robo-runtime"]
    # The system typeface pack (Phase 12.2). The guest discovers `/System/Fonts/` through its manifest
    # and draws the shell's labels with the face it resolves; without the face the desktop still paints,
    # but every label counts into `ROBO_QEMU_DESKTOP_UNSHAPED` instead of reaching the picture. Packed
    # from the repo's own `System/Fonts/` -- the single source of truth for system faces -- rather than
    # fetched at build time, so an offline build and a build on another machine pack the same bytes. A
    # missing face, manifest or licence is a hard build failure: a release image must never ship
    # without its system typeface.
    $systemFonts = Join-Path $script:Repo "System\Fonts"
    $systemFontFace = Join-Path $systemFonts "UI\Inter.ttf"
    $systemFontManifest = Join-Path $systemFonts "system-fonts.manifest"
    $systemFontLicense = Join-Path $systemFonts "licenses\Inter-OFL.txt"
    foreach ($systemFontRequired in @($systemFontFace, $systemFontManifest, $systemFontLicense)) {
        if (-not (Test-Path -LiteralPath $systemFontRequired)) {
            throw ("system font pack incomplete: missing " + $systemFontRequired)
        }
    }
    Add-Pack "/System/Fonts/UI/Inter.ttf" $systemFontFace
    Add-Pack "/System/Fonts/system-fonts.manifest" $systemFontManifest
    Add-Pack "/System/Fonts/licenses/Inter-OFL.txt" $systemFontLicense
    Add-Pack "/System/runtime/engines.manifest" $manifest
    Add-Pack "/bin/robospark" $elfs["robospark"]
    Add-Pack "/bin/robolayer-demo" $elfs["robolayer-demo"]
    Add-Pack "/bin/robosail" $elfs["robosail"]
    Add-Pack "/System/runtime/robospark" $elfs["robospark"]
    Add-Pack "/System/runtime/robolayer-demo" $elfs["robolayer-demo"]
    Add-Pack "/System/runtime/robosail" $elfs["robosail"]
    Add-Pack "/System/runtime/demo.html" (Join-Path $runtime "robosail\demo.html")
    Add-Pack "/test.txt" $txt
    & $py $pack "-o" $initrd @packArgs
    if ($LASTEXITCODE -ne 0) { throw "RoboFS pack failed" }
}

function Invoke-RoboBuildKernel {
    $kroot = $script:KernelRoot
    $kbuild = Join-Path $script:Repo "Build\Kernel"
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
        "-I", (Join-Path $script:Boot "Include"),
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
        (Join-Path $kroot "fs\block\bdev_table.c"),
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
        (Join-Path $kroot "storage\partition.c"),
        (Join-Path $kroot "storage\ahci.c"),
        (Join-Path $kroot "storage\nvme.c"),
        (Join-Path $kroot "graphics\gfx.c"),
        (Join-Path $kroot "graphics\edid.c"),
        (Join-Path $kroot "input\input.c"),
        (Join-Path $kroot "input\ps2.c"),
        (Join-Path $kroot "usb\usb_core.c"),
        (Join-Path $kroot "usb\xhci.c"),
        (Join-Path $kroot "usb\usb_hid.c"),
        (Join-Path $kroot "usb\usb_msc.c"),
        (Join-Path $kroot "power\battery.c"),
        (Join-Path $kroot "ipc\ipc.c"),
        (Join-Path $kroot "display\display.c"),
        (Join-Path $kroot "drivers\virtio_net.c"),
        (Join-Path $kroot "net\core\net_util.c"),
        (Join-Path $kroot "net\core\checksum.c"),
        (Join-Path $kroot "net\core\packet.c"),
        (Join-Path $kroot "net\core\interface.c"),
        (Join-Path $kroot "net\core\network.c"),
        (Join-Path $kroot "net\ethernet\ethernet.c"),
        (Join-Path $kroot "net\arp\arp.c"),
        (Join-Path $kroot "net\ipv4\route.c"),
        (Join-Path $kroot "net\ipv4\ipv4.c"),
        (Join-Path $kroot "net\ipv4\icmp.c"),
        (Join-Path $kroot "net\udp\udp.c"),
        (Join-Path $kroot "net\tcp\tcp.c"),
        (Join-Path $kroot "net\socket\socket.c"),
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
        (Join-Path $kroot "ipc"),
        (Join-Path $kroot "display"),
        (Join-Path $kroot "net\core"),
        (Join-Path $kroot "net\ethernet"),
        (Join-Path $kroot "net\arp"),
        (Join-Path $kroot "net\ipv4"),
        (Join-Path $kroot "net\udp"),
        (Join-Path $kroot "net\tcp"),
        (Join-Path $kroot "net\socket"),
        (Join-Path $kroot "irq"),
        (Join-Path $kroot "arch\x86_64\acpi"),
        (Join-Path $script:Repo "sdk\include\robosys"),
        (Join-Path $script:Boot "Include\roboticela")
    )
    $batch = Invoke-CcDirBatch -Label "kernel" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".o" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        "-I", (Join-Path $script:Boot "Include")
    )
    $srcs = @(
        (Join-Path $kroot "mm\page.c"),
        (Join-Path $kroot "mm\memory_map.c"),
        (Join-Path $kroot "mm\pmm.c"),
        (Join-Path $kroot "mm\pmm_test.c"),
        (Join-Path $script:Repo "Tests\Kernel\test_pmm.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "mm"),
        (Join-Path $kroot "include"),
        (Join-Path $script:Boot "Include\roboticela")
    )
    $batch = Invoke-CcDirBatch -Label "kernel/pmm-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        "-I", (Join-Path $script:Boot "Include")
    )
    $srcs = @(
        (Join-Path $kroot "mm\page.c"),
        (Join-Path $kroot "mm\vmm\paging.c"),
        (Join-Path $script:Repo "Tests\Kernel\test_vmm.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "mm"),
        (Join-Path $kroot "mm\vmm"),
        (Join-Path $kroot "include")
    )
    $batch = Invoke-CcDirBatch -Label "kernel/vmm-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        (Join-Path $script:Repo "Tests\Kernel\test_time.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "time")
    )
    $batch = Invoke-CcDirBatch -Label "kernel/time-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        (Join-Path $script:Repo "Tests\Kernel\test_sched_queue.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "scheduler"),
        (Join-Path $kroot "thread"),
        (Join-Path $kroot "arch\x86_64"),
        (Join-Path $kroot "mm")
    )
    $batch = Invoke-CcDirBatch -Label "kernel/sched-queue-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        (Join-Path $script:Repo "Tests\Kernel\test_thread_state.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "thread")
    )
    $batch = Invoke-CcDirBatch -Label "kernel/thread-state-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        (Join-Path $script:Repo "Tests\Kernel\test_process_state.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "process")
    )
    $batch = Invoke-CcDirBatch -Label "kernel/process-state-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        (Join-Path $script:Repo "Tests\Kernel\test_process_host.c"),
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
    $batch = Invoke-CcDirBatch -Label "kernel/process-host-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        (Join-Path $script:Repo "Tests\Kernel\test_context_layout.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "arch\x86_64"),
        (Join-Path $kroot "arch\x86_64\context")
    )
    $batch = Invoke-CcDirBatch -Label "kernel/context-layout-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        "-I", (Join-Path $script:Boot "Include")
    )
    $srcs = @(
        (Join-Path $kroot "exec\elf\elf.c"),
        (Join-Path $kroot "exec\elf\elf_parser.c"),
        (Join-Path $kroot "exec\elf\elf_validate.c"),
        (Join-Path $kroot "mm\page.c"),
        (Join-Path $kroot "mm\vmm\paging.c"),
        (Join-Path $script:Repo "Tests\Kernel\test_elf.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "exec"),
        (Join-Path $kroot "exec\elf"),
        (Join-Path $kroot "mm"),
        (Join-Path $kroot "mm\vmm"),
        (Join-Path $kroot "include")
    )
    $batch = Invoke-CcDirBatch -Label "kernel/elf-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        (Join-Path $script:Repo "Tests\Kernel\test_rfs.c"),
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
    $batch = Invoke-CcDirBatch -Label "kernel/rfs-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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
        (Join-Path $script:Repo "Tests\Kernel\test_driver_sig.c"),
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
    $batch = Invoke-CcDirBatch -Label "kernel/driver-sig-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
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

function Invoke-RoboBuildCompositorTest {
    $kroot = $script:KernelRoot
    $user = Join-Path $script:Repo "user"
    $kobj = Join-Path $kroot "build\obj\compositor-host-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-I", (Join-Path $script:Repo "sdk\include")
    )
    $srcs = @(
        (Join-Path $user "lib\compose.c"),
        (Join-Path $user "services\compositor\test_host.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $script:Repo "sdk\include\robocompositor")
    )
    $batch = Invoke-CcDirBatch -Label "kernel/compositor-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:CompositorTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_compositor.exe"
        & $clang -o $script:CompositorTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "compositor host test link failed" }
    }
}

function Invoke-RoboTestCompositor {
    Invoke-RoboBuildCompositorTest
    Write-Step "TEST" "test_compositor.exe"
    & $script:CompositorTestOut
    if ($LASTEXITCODE -ne 0) { throw "compositor host tests failed" }
}

function Invoke-RoboBuildNetTest {
    $kroot = $script:KernelRoot
    $kobj = Join-Path $kroot "build\obj\net-host-test"
    New-Item -ItemType Directory -Force -Path $kobj | Out-Null
    $clang = Get-Clang
    $cflags = @(
        "-std=c11", "-O2",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-DNET_HOST_TEST",
        "-I", $kroot,
        "-I", (Join-Path $kroot "include"),
        "-I", (Join-Path $script:Repo "sdk\include")
    )
    $srcs = @(
        (Join-Path $kroot "net\core\checksum.c"),
        (Join-Path $kroot "net\core\net_util.c"),
        (Join-Path $kroot "net\ipv4\ipv4.c"),
        (Join-Path $kroot "net\ethernet\ethernet.c"),
        (Join-Path $kroot "net\tcp\tcp.c"),
        (Join-Path $script:Repo "Tests\Kernel\test_net.c")
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "net\core"),
        (Join-Path $kroot "net\ethernet"),
        (Join-Path $kroot "net\ipv4"),
        (Join-Path $kroot "net\tcp")
    )
    $batch = Invoke-CcDirBatch -Label "kernel/net-test" -Clang $clang -CFlags $cflags `
        -Sources $srcs -ObjDir $kobj -Ext ".obj" -Deps $deps
    $objs = @($batch.Objects)
    $compiled = [bool]$batch.Compiled
    if ($script:RoboRebuild -or $compiled -or (Test-LinkStale $script:NetTestOut $objs)) {
        Write-Step "LD" "kernel\build\test_net.exe"
        & $clang -o $script:NetTestOut @objs
        if ($LASTEXITCODE -ne 0) { throw "net host test link failed" }
    }
}

function Invoke-RoboTestNet {
    Invoke-RoboBuildNetTest
    Write-Step "TEST" "test_net.exe"
    & $script:NetTestOut
    if ($LASTEXITCODE -ne 0) { throw "net host tests failed" }
}

function Invoke-RoboTestLibc {
    $clang = Get-Clang
    $libc = Join-Path $script:Repo "Libc"
    $out = Join-Path $script:Repo "Build\Libc\test_libc.exe"
    New-Item -ItemType Directory -Force -Path (Join-Path $script:Repo "Build\Libc") | Out-Null
    $inc = Join-Path $libc "include"
    $srcs = @(
        (Join-Path $script:Repo "Tests\Libc\libc_host_test.c"),
        (Join-Path $libc "src\string\string.c"),
        (Join-Path $libc "src\errno\errno.c"),
        (Join-Path $libc "src\memory\malloc.c"),
        (Join-Path $libc "src\stdlib\stdlib.c"),
        (Join-Path $libc "src\stdio\stdio.c"),
        (Join-Path $libc "src\env\env.c")
    )
    Write-Step "TEST" "libc host"
    & $clang "-std=c11" "-O2" "-ffreestanding" "-fno-stack-protector" "-nostdlib" `
        "-DLIBC_HOST_TEST" "-Wno-format-truncation" "-I" $inc "-o" $out @srcs `
        "-lkernel32" "-Xlinker" "/entry:main" "-Xlinker" "/subsystem:console"
    if ($LASTEXITCODE -ne 0) { throw "libc host compile failed" }
    & $out
    if ($LASTEXITCODE -ne 0) { throw "libc host tests failed" }
}

function Invoke-RoboBuildImage {
    param([string]$Flags = "")
    Invoke-RoboBuildKernel
    if (-not (Test-Path $script:EfiOut)) { throw "BOOTX64.EFI missing - build efi first" }
    $py = Get-Python
    $manifest = Join-Path $script:Boot "Config\roboboot.manifest"
    $mkesp = Join-Path $script:Boot "Tools\make_esp.py"
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
        (Join-Path $script:Boot "Tools\make_esp.py"),
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
    if ($text -match '(?m)^debug=') {
        $text = [regex]::Replace($text, '(?m)^debug=.*$', 'debug=0')
    } else {
        $text = $text.TrimEnd("`n") + "`ndebug=0`n"
    }
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
    foreach ($junk in @("roboboot.exe", "roboboot-esp.img", "COMBINED.txt")) {
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

    $sys = Join-Path $script:Dist "volumes\sys"
    New-Item -ItemType Directory -Force -Path $sys | Out-Null
    $kern = Join-Path $sys "kernel.elf"
    $initrd = Join-Path $sys "initrd"
    if (-not (Test-Path $kern) -and (Test-Path $script:KernelOut)) {
        Copy-Item $script:KernelOut $kern -Force
    }
    $initrdSrc = Join-Path $script:Boot "volumes\sys\initrd"
    if (-not (Test-Path $initrd) -and (Test-Path $initrdSrc)) {
        Copy-Item $initrdSrc $initrd -Force
    }
    if (-not (Test-Path $efiBoot)) { throw "combine missing EFI\BOOT\BOOTX64.EFI" }
    if (-not (Test-Path $kern)) { throw "combine missing volumes\sys\kernel.elf" }
    if (-not (Test-Path $initrd)) { throw "combine missing volumes\sys\initrd" }

    Copy-Item $script:EfiOut (Join-Path $script:Dist "BOOTX64.EFI") -Force
    Copy-Item $kern (Join-Path $script:Dist "kernel.elf") -Force
    Copy-Item $initrd (Join-Path $script:Dist "initrd") -Force
    if (Test-Path $script:EspImg) {
        Copy-Item $script:EspImg (Join-Path $script:Dist "roboticelaos.img") -Force
    }

    $note = @(
        "RoboticelaOS - this folder is the whole OS",
        "",
        "Copy onto a FAT32 USB stick (file tree)",
        "  1. Format the stick as FAT32.",
        "  2. Copy EFI\, config\, assets\ and volumes\ onto the stick root.",
        "     Do not copy roboticelaos.img, the .zip, or the loose BOOTX64.EFI / kernel.elf / initrd",
        "     onto the stick - those are for flashing or inspection.",
        "  3. The stick root must contain EFI\BOOT\BOOTX64.EFI (do not nest an extra folder).",
        "  4. On the PC: disable Secure Boot, boot from USB in UEFI mode (not Legacy / CSM).",
        "",
        "Write the disk image to the whole device",
        "  roboticelaos.img  - GPT disk with the ESP; flash it with:",
        "    scripts\robo flash --device N --yes",
        "",
        "Layout",
        "  EFI\BOOT\BOOTX64.EFI         default firmware boot path",
        "  EFI\Roboticela\BOOTX64.EFI   vendor boot path",
        "  config\                      catalog and saved settings",
        "  assets\                      splash, pictures, fonts",
        "  volumes\sys\kernel.elf       kernel on the stick",
        "  volumes\sys\initrd           initrd on the stick",
        "  roboticelaos.img             whole-disk image (same OS)",
        "  roboticelaos-usb.zip         the EFI/config/assets/volumes tree zipped",
        "  BOOTX64.EFI, kernel.elf, initrd   loose copies of the same binaries"
    )
    [IO.File]::WriteAllLines((Join-Path $script:Dist "USB.txt"), $note)
    Write-Step "USB" $script:Dist
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
    Invoke-RoboTestCompositor
    Invoke-RoboTestNet
    Invoke-RoboTestLibc
}

function Invoke-RoboClean {
    if (Test-Path $script:Build) {
        Write-Step "CLEAN" "bootloader\build"
        Remove-Item $script:Build -Recurse -Force
    }
    $kbuild = Join-Path $script:Repo "Build\Kernel"
    if (Test-Path $kbuild) {
        Write-Step "CLEAN" "Build\Kernel"
        Remove-Item $kbuild -Recurse -Force
    }
    $lbuild = Join-Path $script:Repo "Build\Libc"
    if (Test-Path $lbuild) {
        Write-Step "CLEAN" "Build\Libc"
        Remove-Item $lbuild -Recurse -Force
    }
    if (Test-Path $script:Dist) {
        Write-Step "CLEAN" "Build\dist"
        Remove-Item $script:Dist -Recurse -Force
    }
}

function Invoke-RoboDistclean {
    Invoke-RoboClean
    $baked = @()
    $baked += Get-ChildItem (Join-Path $script:Boot "assets") -Filter *.rbim -File -ErrorAction SilentlyContinue
    $baked += Get-ChildItem (Join-Path $script:Boot "Assets\fonts") -Filter *.rbfn -File -ErrorAction SilentlyContinue
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
