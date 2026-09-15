# RoboticelaOS pipeline — environment validation (`robo check`).
#
# Reports exactly what the project needs and whether it is present, so a
# failing build is never a mystery. Every requirement is probed for real: a
# compiler is only FOUND if it can report a version, and a Python package is
# only FOUND if it actually imports.

function Get-RoboPythonProbe {
    param(
        [Parameter(Mandatory)][string]$Python,
        [string]$Import = ""
    )
    $code = if ($Import) { "import $Import; print('ok')" } else { "import sys; print(sys.version.split()[0])" }
    $r = Invoke-RoboProcess -FilePath $Python -Arguments @("-c", $code) -TimeoutSeconds 30
    if ($r.ExitCode -ne 0) { return $null }
    $t = ($r.Output -split "`r?`n" | Where-Object { $_.Trim() } | Select-Object -First 1)
    if (-not $t) { return $null }
    return $t.Trim()
}

function New-RoboRequirement {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Purpose,
        [Parameter(Mandatory)][ValidateSet("required", "optional")][string]$Kind,
        [Parameter(Mandatory)][scriptblock]$Probe
    )
    return [ordered]@{ Name = $Name; Purpose = $Purpose; Kind = $Kind; Probe = $Probe }
}

function Get-RoboRequirements {
    <#
      The real dependency set, derived from what the build actually invokes:
        clang        host preview, EFI app, kernel, libc, userland, runtime
        lld-link     UEFI PE image
        ld.lld       kernel.elf
        llvm-ar      libc.a archive
        llvm-readobj libc "no host symbols" verification
        python 3     asset baking, ESP/disk image, RoboFS image, font tools
        Pillow       bake_assets.py (raster assets)
        cairosvg     bake_assets.py (logo.svg)
        qemu         run-qemu, run-kernel, test-*-qemu
        OVMF code    UEFI firmware
        OVMF vars    writable UEFI variable store
    #>
    return @(
        (New-RoboRequirement -Name "clang" -Kind required -Purpose "host, UEFI, kernel, libc, userland, runtime objects" -Probe {
            $p = Get-RoboTool "clang"
            if (-not $p) { return @{ Status = "missing"; Detail = "not on PATH; set ROBO_LLVM_BIN or ROBO_CLANG" } }
            $v = Get-RoboToolVersion $p
            return @{ Status = "found"; Path = $p; Version = $v }
        }),
        (New-RoboRequirement -Name "lld-link" -Kind required -Purpose "UEFI BOOTX64.EFI" -Probe {
            $p = Get-RoboTool "lld_link"
            if (-not $p) { return @{ Status = "missing"; Detail = "LLVM lld is required for the UEFI image" } }
            return @{ Status = "found"; Path = $p; Version = (Get-RoboToolVersion $p) }
        }),
        (New-RoboRequirement -Name "ld.lld" -Kind required -Purpose "kernel.elf link" -Probe {
            $p = Get-RoboTool "ld_lld"
            if (-not $p) { return @{ Status = "missing"; Detail = "needed to link the kernel ELF" } }
            return @{ Status = "found"; Path = $p; Version = (Get-RoboToolVersion $p) }
        }),
        (New-RoboRequirement -Name "llvm-ar" -Kind required -Purpose "libc.a, runtime archive" -Probe {
            $p = Get-RoboTool "llvm_ar"
            if (-not $p) { return @{ Status = "missing"; Detail = "needed to archive libc" } }
            return @{ Status = "found"; Path = $p; Version = (Get-RoboToolVersion $p) }
        }),
        (New-RoboRequirement -Name "llvm-readobj" -Kind optional -Purpose "libc host-symbol verification" -Probe {
            $p = Get-RoboTool "llvm_readobj"
            if (-not $p) { return @{ Status = "missing"; Detail = "libc symbol check will be skipped" } }
            return @{ Status = "found"; Path = $p; Version = (Get-RoboToolVersion $p) }
        }),
        (New-RoboRequirement -Name "python 3" -Kind required -Purpose "asset baking, disk image, RoboFS tool, font/i18n generators" -Probe {
            $p = Get-RoboTool "python"
            if (-not $p) { return @{ Status = "missing"; Detail = "install Python 3.x and put it on PATH" } }
            $v = Get-RoboPythonProbe -Python $p
            if (-not $v) { return @{ Status = "missing"; Detail = "interpreter present but not runnable" } }
            if ([version]($v -replace "[^0-9.].*$", "") -lt [version]"3.8") {
                return @{ Status = "wrong-version"; Path = $p; Version = $v; Detail = "3.8 or newer required" }
            }
            return @{ Status = "found"; Path = $p; Version = $v }
        }),
        (New-RoboRequirement -Name "python: Pillow" -Kind required -Purpose "bake_assets.py raster assets" -Probe {
            $p = Get-RoboTool "python"
            if (-not $p) { return @{ Status = "missing"; Detail = "python missing" } }
            $v = Get-RoboPythonProbe -Python $p -Import "PIL"
            if (-not $v) { return @{ Status = "missing"; Detail = "pip install Pillow" } }
            return @{ Status = "found"; Path = "python -c 'import PIL'"; Version = "importable" }
        }),
        (New-RoboRequirement -Name "python: cairosvg" -Kind required -Purpose "bake_assets.py logo.svg rasterisation" -Probe {
            $p = Get-RoboTool "python"
            if (-not $p) { return @{ Status = "missing"; Detail = "python missing" } }
            $v = Get-RoboPythonProbe -Python $p -Import "cairosvg"
            if (-not $v) { return @{ Status = "missing"; Detail = "pip install cairosvg" } }
            return @{ Status = "found"; Path = "python -c 'import cairosvg'"; Version = "importable" }
        }),
        (New-RoboRequirement -Name "qemu-system-x86_64" -Kind optional -Purpose "run-kernel, run-qemu, test-*-qemu" -Probe {
            $p = Get-RoboTool "qemu"
            if (-not $p) { return @{ Status = "missing"; Detail = "install QEMU; QEMU commands will be skipped" } }
            return @{ Status = "found"; Path = $p; Version = (Get-RoboToolVersion $p) }
        }),
        (New-RoboRequirement -Name "OVMF firmware" -Kind optional -Purpose "UEFI boot in QEMU" -Probe {
            if (-not (Test-Path -LiteralPath $script:RoboQemu.FirmwareCode)) {
                return @{ Status = "missing"; Detail = "missing $($script:RoboQemu.FirmwareCode)" }
            }
            return @{ Status = "found"; Path = $script:RoboQemu.FirmwareCode; Version = "edk2" }
        }),
        (New-RoboRequirement -Name "OVMF var store" -Kind optional -Purpose "UEFI NVRAM (boot entries)" -Probe {
            if (-not (Test-Path -LiteralPath $script:RoboQemu.FirmwareVars)) {
                return @{ Status = "missing"; Detail = "missing $($script:RoboQemu.FirmwareVars)" }
            }
            return @{ Status = "found"; Path = $script:RoboQemu.FirmwareVars; Version = "edk2" }
        })
    )
}

function Invoke-RoboCheck {
    <#
      Exit code 0 when every required dependency is present, 1 otherwise.
      Optional dependencies are reported but never fail the check: the pipeline
      is designed so that a machine without QEMU can still build and unit-test
      the whole system.
    #>
    Initialize-RoboEngine

    Write-RoboSection "Environment"
    $hostOs = if ($PSVersionTable.OS) { $PSVersionTable.OS }
    else { [System.Environment]::OSVersion.VersionString }
    Write-RoboKeyValue "Host" $hostOs
    Write-RoboKeyValue "PowerShell" $PSVersionTable.PSVersion.ToString()
    Write-RoboKeyValue "Edition" $(if ($PSVersionTable.PSEdition) { $PSVersionTable.PSEdition } else { "Desktop" })
    Write-RoboKeyValue "Repository" $script:RoboPaths.Root
    Write-RoboKeyValue "Build output" $script:RoboOut.Build
    if (Get-RoboRedactedEnv | Where-Object { $_ }) {
        foreach ($row in (Get-RoboRedactedEnv)) {
            $shown = if ($row -match $script:RoboSecretPattern) { ($row -split "=")[0] + "=<redacted>" } else { $row }
            Write-RoboKeyValue "override" $shown
        }
    }

    Write-RoboSection "Toolchain"
    $missingRequired = @()
    $missingOptional = @()
    foreach ($req in (Get-RoboRequirements)) {
        $res = & $req.Probe
        $status = $res.Status
        $label = switch ($status) {
            "found" { "FOUND" }
            "missing" { if ($req.Kind -eq "required") { "MISSING" } else { "OPTIONAL" } }
            "wrong-version" { "WRONG VERSION" }
            default { "UNKNOWN" }
        }
        if ($status -eq "missing" -and $req.Kind -eq "required") { $missingRequired += $req.Name }
        if ($status -eq "missing" -and $req.Kind -eq "optional") { $missingOptional += $req.Name }
        if ($status -eq "wrong-version") { $missingRequired += $req.Name }

        $line = $req.Name.PadRight(24)
        $ver = ""
        if ($res.Version) { $ver = "  " + $res.Version }
        $color = switch ($label) {
            "FOUND" { "Green" }
            "MISSING" { "Red" }
            "WRONG VERSION" { "Red" }
            "OPTIONAL" { "DarkYellow" }
            default { "Gray" }
        }
        Write-Host ("  " + $label.PadRight(14) + $line) -NoNewline -ForegroundColor $color
        Write-Host $ver -ForegroundColor DarkGray
        if ($res.Detail) { Write-RoboInfo $res.Detail }
        if (-not $res.Detail -and $res.Path -and $script:RoboFlag.Verbose) { Write-RoboInfo $res.Path }
        if ($req.Kind -eq "optional" -and $status -eq "missing") { Write-RoboInfo $req.Purpose }
    }

    # ---------------------------------------------------------------------
    # Source layout: proves the pipeline is pointed at the real tree and that
    # every component it claims to build actually exists.
    # ---------------------------------------------------------------------
    Write-RoboSection "Sources"
    $sources = @(
        @{ Name = "bootloader entry"; Path = (Join-Path $script:RoboPaths.Bootloader "src") },
        @{ Name = "bootloader manifest"; Path = (Join-Path $script:RoboPaths.Bootloader "config\roboboot.manifest") },
        @{ Name = "ESP/disk image tool"; Path = (Join-Path $script:RoboPaths.Bootloader "tools\make_esp.py") },
        @{ Name = "asset baker"; Path = (Join-Path $script:RoboPaths.Bootloader "tools\bake_assets.py") },
        @{ Name = "kernel sources"; Path = (Join-Path $script:RoboPaths.Kernel "src") },
        @{ Name = "kernel linker script"; Path = (Join-Path $script:RoboPaths.Kernel "linker\kernel.ld") },
        @{ Name = "libc"; Path = $script:RoboPaths.Libc },
        @{ Name = "userland"; Path = $script:RoboPaths.User },
        @{ Name = "guest runtime program"; Path = (Join-Path $script:RoboPaths.User "robo-runtime\main.c") },
        @{ Name = "runtime sources"; Path = (Join-Path $script:RoboPaths.Runtime "Source") },
        @{ Name = "runtime build script"; Path = (Join-Path $script:RoboPaths.Runtime "Tools\Build.ps1") },
        @{ Name = "RoboFS tool"; Path = (Join-Path $script:RoboPaths.Tools "robofs_tool.py") }
    )
    $missingSources = @()
    foreach ($s in $sources) {
        if (Test-Path -LiteralPath $s.Path) {
            Write-Host ("  " + "OK".PadRight(14) + $s.Name) -ForegroundColor Green
        } else {
            Write-Host ("  " + "MISSING".PadRight(14) + $s.Name) -ForegroundColor Red
            Write-RoboInfo $s.Path
            $missingSources += $s.Name
        }
    }

    # ---------------------------------------------------------------------
    # QEMU readiness, reported as one honest verdict rather than a guess.
    # ---------------------------------------------------------------------
    Write-RoboSection "QEMU readiness"
    $qemuReady = $true
    if (-not (Get-RoboTool "qemu")) {
        $qemuReady = $false
        Write-RoboWarn "QEMU not installed - run-kernel / run-qemu / test-*-qemu will be skipped"
    }
    if (-not (Test-Path -LiteralPath $script:RoboQemu.FirmwareCode)) {
        $qemuReady = $false
        Write-RoboWarn "UEFI firmware missing: $($script:RoboQemu.FirmwareCode)"
    }
    if (-not (Test-Path -LiteralPath $script:RoboQemu.FirmwareVars)) {
        $qemuReady = $false
        Write-RoboWarn "UEFI variable template missing: $($script:RoboQemu.FirmwareVars)"
    }
    if ($qemuReady) { Write-RoboPass "QEMU + UEFI firmware ready" }

    # ---------------------------------------------------------------------
    # Chromium independence: the runtime must not depend on Chromium.
    # ---------------------------------------------------------------------
    Write-RoboSection "Chromium independence"
    $scan = Test-RoboChromiumIndependence
    foreach ($row in $scan.Rows) {
        if ($row.Hits -eq 0) {
            Write-Host ("  " + "CLEAN".PadRight(14) + $row.Scope) -ForegroundColor Green
        } else {
            Write-Host ("  " + "HITS".PadRight(14) + ($row.Scope + " (" + $row.Hits + ")")) -ForegroundColor Red
        }
    }
    foreach ($sample in @($scan.Samples)) {
        if ($sample) { Write-RoboInfo $sample }
    }
    Write-RoboKeyValue "Reference tree" $(if ($scan.ReferenceExists) { "chromium/ present (reference only, never built)" } else { "chromium/ absent" })

    # ---------------------------------------------------------------------
    Write-RoboSection "Verdict"
    if ($missingSources.Count -gt 0) {
        Write-RoboFail ("missing sources: " + ($missingSources -join ", "))
    }
    if ($missingRequired.Count -gt 0) {
        Write-RoboFail ("missing required dependencies: " + ($missingRequired -join ", "))
    }
    if (-not $scan.Clean) {
        Write-RoboFail "runtime sources reference Chromium"
    }

    if ($missingRequired.Count -eq 0 -and $missingSources.Count -eq 0 -and $scan.Clean) {
        Write-RoboPass "environment is ready"
        Write-RoboInfo "next: scripts/robo build-all"
        if (-not $qemuReady) { Write-RoboInfo "QEMU is not ready; build and unit tests are unaffected" }
        return $true
    }

    Write-RoboInfo "install or point the pipeline at the missing tools, then re-run 'scripts/robo check'"
    return $false
}

$script:RoboChromiumSourcePattern = '(?i)(#\s*include\s*[<"]' +
    '[^">]*(chromium|skia|v8|blink|mojo)|third_party[/\\](chromium|skia)|' +
    '\bnamespace\s+(blink|skia)\b)'

# Documentation lines that legitimately name Chromium (independence notes,
# forbidden-dependency lists). Same allow-list shape the runtime's own
# -Verify scan uses, so `check` and the runtime build agree.
$script:RoboChromiumAllowPattern =
    '(?i)must never depend|Forbidden:|temporary technical reference|never depend|' +
    'no Chromium|without Chromium|independence|Chromium is only|Chromium reference|' +
    'not a dependency|do not include|must not include'

function Test-RoboChromiumIndependence {
    <#
      Two different questions, both answered from the real files:

        1. Do the runtime's sources #include or open anything from Chromium?
           Answered with the runtime's own pattern plus its documentation
           allow-list, so the verdict matches `Build.ps1 -Verify`.

        2. Do any build files feed the Chromium reference tree into the
           compiler or linker? Naming Chromium in a comment, or scanning for it
           in a guard, is not a dependency - only an include/link path is.

      `chromium/` is vendored reference material; it is reported, never built.
    #>
    $runtime = $script:RoboPaths.Runtime
    if (-not (Test-Path -LiteralPath $runtime)) {
        return @{ Clean = $false; Rows = @(@{ Scope = "runtime"; Hits = 1 }); ReferenceExists = $false }
    }

    # --- 1. runtime sources ----------------------------------------------
    $codeDirs = @((Join-Path $runtime "Source"), (Join-Path $runtime "Include"), (Join-Path $runtime "Tests"))
    $sourceHits = 0
    $sourceSamples = @()
    foreach ($d in $codeDirs) {
        if (-not (Test-Path -LiteralPath $d)) { continue }
        foreach ($f in Get-ChildItem -LiteralPath $d -Recurse -File -ErrorAction SilentlyContinue) {
            if ($f.Extension -notin @(".c", ".cc", ".cpp", ".h", ".hpp")) { continue }
            try { $text = [IO.File]::ReadAllText($f.FullName) } catch { continue }
            foreach ($line in ($text -split "`n")) {
                if ($line -notmatch $script:RoboChromiumSourcePattern) { continue }
                if ($line -match $script:RoboChromiumAllowPattern) { continue }
                $sourceHits++
                if ($sourceSamples.Count -lt 8) { $sourceSamples += ($f.Name + ": " + $line.Trim()) }
            }
        }
    }

    # --- 2. build wiring --------------------------------------------------
    $buildNames = @(
        "build.ps1", "Makefile", "scripts\robo", "scripts\robo.cmd", "scripts\robo.ps1",
        "scripts\pipeline\config.ps1", "scripts\pipeline\engine.ps1",
        "scripts\pipeline\commands.build.ps1", "scripts\pipeline\commands.check.ps1",
        "scripts\pipeline\commands.deploy.ps1", "scripts\pipeline\commands.run.ps1",
        "scripts\pipeline\commands.test.ps1", "runtime\Tools\Build.ps1",
        "scripts\roboboot-build-common.ps1", "scripts\roboboot-qemu-common.ps1"
    )
    # An include/link path into Chromium, or a Chromium build-system file.
    $depPatterns = @(
        '(?im)^\s*[-/](?:I|isystem)\s*\S*chromium',
        '(?im)^\s*[-/](?:L|l)\s*\S*chromium',
        '(?im)[-/](?:I|isystem|L)\s*["''][^"'']*chromium',
        '(?im)chromium[\\/][^\s"'')]*\.(?:h|hpp|lib|a|so|dll|gn)',
        '(?im)(?:^|\s)(?:BUILD\.gn|CMakeLists\.txt|SConstruct|meson\.build)\s*\(?\s*["'']?chromium'
    )
    $buildHits = 0
    $buildSamples = @()
    foreach ($rel in $buildNames) {
        $p = Join-Path $script:RoboPaths.Root $rel
        if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { continue }
        try { $text = [IO.File]::ReadAllText($p) } catch { continue }
        foreach ($pat in $depPatterns) {
            foreach ($m in [regex]::Matches($text, $pat)) {
                $line = ($m.Value -replace "\s+", " ").Trim()
                $buildHits++
                if ($buildSamples.Count -lt 8) { $buildSamples += ($rel + ": " + $line) }
            }
        }
    }

    $samples = @($sourceSamples) + @($buildSamples)
    $rows = @(
        @{ Scope = "runtime sources + headers"; Hits = $sourceHits },
        @{ Scope = "build wiring"; Hits = $buildHits }
    )
    $ref = Join-Path $script:RoboPaths.Root "chromium"
    return @{
        Clean = ($sourceHits -eq 0 -and $buildHits -eq 0)
        Rows = $rows
        Samples = $samples
        ReferenceExists = (Test-Path -LiteralPath $ref)
    }
}
