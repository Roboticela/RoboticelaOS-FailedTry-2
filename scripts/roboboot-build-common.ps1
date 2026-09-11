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
        "-mno-red-zone", "-fno-builtin", "-nostdlib",
        "-I", (Join-Path $kroot "include"),
        "-I", $kroot,
        "-I", (Join-Path $script:Boot "include")
    )
    $srcs = @(
        (Join-Path $kroot "arch\x86_64\boot\start.S"),
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
        $kfont
    )
    $deps = Get-HeaderDeps @(
        (Join-Path $kroot "include"),
        (Join-Path $kroot "mm"),
        (Join-Path $kroot "mm\vmm"),
        (Join-Path $kroot "mm\heap"),
        (Join-Path $kroot "arch\x86_64\mm"),
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

function Invoke-RoboBuildImage {
    param([string]$Flags = "")
    Invoke-RoboBuildKernel
    if (-not (Test-Path $script:EfiOut)) { throw "BOOTX64.EFI missing - build efi first" }
    $py = Get-Python
    $manifest = Join-Path $script:Boot "config\roboboot.manifest"
    $mkesp = Join-Path $script:Boot "tools\make_esp.py"
    $kern = Join-Path $script:Boot "volumes\sys\kernel.elf"
    $deps = @($script:EfiOut, $manifest, $mkesp, $kern)
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
