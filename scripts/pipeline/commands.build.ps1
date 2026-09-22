# RoboticelaOS pipeline — build commands.
#
# Dependency order is the real one:
#
#   assets ─┐
#           ├─> bootloader EFI ─┐
#   runtime ┘                   ├─> kernel.elf ─> ESP/disk image ─> deploy tree
#   libc ───> userland ─────────┘
#
# `build-all` walks that order and refuses to claim success unless each
# artifact exists and is non-empty afterwards.

function Publish-RoboArtifacts {
    <#
      Copies the canonical artifacts the engines produced into build/artifacts/
      and the flash-drive image into build/images/, so a developer has one
      directory to look at regardless of which subsystem generated what.
    #>
    Initialize-RoboOutputTree
    $copies = @(
        @{ Source = $script:EfiOut; Name = "BOOTX64.EFI"; What = "UEFI bootloader" },
        @{ Source = $script:KernelOut; Name = "kernel.elf"; What = "kernel" }
    )
    if (Test-Path -LiteralPath $script:HostOut) {
        $copies += @{ Source = $script:HostOut; Name = "roboboot.exe"; What = "host preview" }
    }
    if (Test-Path -LiteralPath $script:TestOut) {
        $copies += @{ Source = $script:TestOut; Name = "roboboot-test.exe"; What = "bootloader tests" }
    }
    $copied = @()
    foreach ($c in $copies) {
        Assert-RoboFile $c.Source $c.What | Out-Null
        $dst = Join-Path $script:RoboOut.Artifacts $c.Name
        Copy-Item -LiteralPath $c.Source -Destination $dst -Force
        $copied += (Get-Item -LiteralPath $dst)
    }

    # The ESP image IS the deployable artifact: make_esp.py writes a full GPT
    # disk with a FAT16 ESP, so it can be written straight to a USB stick.
    Assert-RoboFile $script:RoboEngineImage "disk image" | Out-Null
    if (-not (Test-Path -LiteralPath $script:RoboOut.Images)) {
        New-Item -ItemType Directory -Force -Path $script:RoboOut.Images | Out-Null
    }
    Copy-Item -LiteralPath $script:RoboEngineImage -Destination $script:RoboArtifact.DeployImage -Force
    $copied += (Get-Item -LiteralPath $script:RoboArtifact.DeployImage)

    $zip = Publish-RoboUsbArchive
    if ($zip) { $copied += $zip }

    return $copied
}

function Publish-RoboUsbArchive {
    <#
      Zips the contents of Build/dist so unzipping onto a FAT32 stick produces
      EFI\BOOT\BOOTX64.EFI at the root with no nested folder.
    #>
    $tree = Get-RoboDeploymentTree
    $zip = Join-Path $script:RoboOut.Images "roboticelaos-usb.zip"
    if (-not (Test-Path -LiteralPath $tree)) {
        throw "USB tree missing: $tree"
    }
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    $items = @(Get-ChildItem -LiteralPath $tree -Force)
    if ($items.Count -eq 0) { throw "deployment tree is empty: $tree" }
    if (-not (Test-Path -LiteralPath $script:RoboOut.Images)) {
        New-Item -ItemType Directory -Force -Path $script:RoboOut.Images | Out-Null
    }
    Compress-Archive -Path ($items | ForEach-Object { $_.FullName }) -DestinationPath $zip -Force -CompressionLevel Optimal
    Assert-RoboFile $zip "USB archive" | Out-Null
    return (Get-Item -LiteralPath $zip)
}

function New-RoboDeploymentManifest {
    <#
      Records what was built so `validate-deployment` can check the tree
      without re-deriving anything, and so an image can be traced back to a
      revision.
    #>
    param([object[]]$Artifacts = @())

    $deploy = Get-RoboDeploymentTree
    $manifest = [ordered]@{
        schema        = "roboticelaos.deployment/1"
        built         = (Get-Date).ToString("o")
        target        = $script:RoboConfig.TargetArch
        configuration = $script:RoboConfig.RuntimeConfig
        root          = $script:RoboPaths.Root
        boot          = @{
            efi        = "EFI/BOOT/BOOTX64.EFI"
            vendorEfi  = "EFI/Roboticela/BOOTX64.EFI"
            kernel     = "volumes/sys/kernel.elf"
            initrd     = "volumes/sys/initrd"
        }
        diskImage     = $script:RoboArtifact.DeployImage
        artifacts     = @($Artifacts | ForEach-Object {
                [ordered]@{ name = $_.Name; bytes = $_.Length }
            })
    }

    # Revision is advisory: a source tree exported without .git still builds.
    $rev = ""
    $r = Invoke-RoboProcess -FilePath "git" -Arguments @("rev-parse", "--short", "HEAD") `
        -WorkingDirectory $script:RoboPaths.Root -TimeoutSeconds 20
    if ($r.ExitCode -eq 0) { $rev = $r.Output.Trim() }
    if ($rev) { $manifest["revision"] = $rev }

    $branch = Invoke-RoboProcess -FilePath "git" -Arguments @("rev-parse", "--abbrev-ref", "HEAD") `
        -WorkingDirectory $script:RoboPaths.Root -TimeoutSeconds 20
    if ($branch.ExitCode -eq 0 -and $branch.Output.Trim()) { $manifest["branch"] = $branch.Output.Trim() }

    New-Item -ItemType Directory -Force -Path $script:RoboOut.Deploy | Out-Null
    Write-RoboUtf8File -Path $script:RoboArtifact.DeployManifest -Text ($manifest | ConvertTo-Json -Depth 6)
    return $script:RoboArtifact.DeployManifest
}

# --------------------------------------------------------------------------
# Individual build commands
# --------------------------------------------------------------------------

function Invoke-RoboBuildCheck {
    if (-not (Invoke-RoboCheck)) {
        throw "environment check failed; fix the reported problems before building"
    }
}

function Invoke-RoboBuildAssets {
    Invoke-RoboStep -Name "bake bootloader assets" -Layer "BUILD" -Action {
        Invoke-RoboEngineAssets
        foreach ($f in @("logo.rbim", "fonts\ui-title.rbfn", "fonts\ui-body.rbfn", "fonts\ui-caption.rbfn")) {
            Assert-RoboFile (Join-Path $script:RoboPaths.Bootloader "assets\$f") "baked asset $f" | Out-Null
        }
    } | Out-Null
}

function Invoke-RoboBuildRuntimeCommand {
    param([switch]$Continue, [switch]$Sanitize, [string]$Filter = "")
    Invoke-RoboEngineRuntime -Configuration $script:RoboConfig.RuntimeConfig -Verify `
        -Sanitize:$Sanitize -Filter $Filter -Continue:$Continue -LogName "build-runtime" | Out-Null
}

function Invoke-RoboBuildBootloader {
    Invoke-RoboStep -Name "bootloader host preview" -Layer "BUILD" -Action {
        Invoke-RoboEngineBootloaderHost
        Assert-RoboFile $script:HostOut "host preview binary" | Out-Null
    } | Out-Null

    Invoke-RoboStep -Name "bootloader UEFI application" -Layer "BUILD" -Action {
        Invoke-RoboEngineBootloaderEfi
    } | Out-Null
}

function Invoke-RoboBuildBootloaderTests {
    Invoke-RoboStep -Name "bootloader test binary" -Layer "BUILD" -Action {
        Invoke-RoboEngineBootloaderTestBinary
    } | Out-Null
}

function Invoke-RoboBuildUserlandCommand {
    Invoke-RoboStep -Name "libc + userland + guest runtime" -Layer "BUILD" -Action {
        Invoke-RoboEngineUserland
    } | Out-Null
}

function Invoke-RoboBuildKernelCommand {
    Invoke-RoboStep -Name "kernel.elf (libc, userland, initrd)" -Layer "BUILD" -Action {
        Invoke-RoboEngineKernel
    } | Out-Null
}

function Invoke-RoboBuildImageCommand {
    param([string]$Flags = "")
    Invoke-RoboStep -Name "ESP + GPT disk image" -Layer "BUILD" -Action {
        Invoke-RoboEngineCombine -Flags $Flags
        $tree = Get-RoboDeploymentTree
        Assert-RoboFile (Join-Path $tree "EFI\BOOT\BOOTX64.EFI") "deployment bootloader" | Out-Null
        Assert-RoboFile (Join-Path $tree "volumes\sys\kernel.elf") "deployment kernel" | Out-Null
        Assert-RoboFile (Join-Path $tree "volumes\sys\initrd") "deployment initrd" | Out-Null
    } | Out-Null
}

function Invoke-RoboBuildArtifactsCommand {
    Invoke-RoboStep -Name "publish artifacts + deployment manifest" -Layer "BUILD" -Action {
        $copied = Publish-RoboArtifacts
        Write-RoboInfo ("artifacts: " + $script:RoboOut.Artifacts)
        foreach ($c in $copied) {
            Write-RoboInfo ("  " + $c.Name.PadRight(22) + (Get-RoboFileSizeText $c.Length))
        }
        New-RoboDeploymentManifest -Artifacts $copied | Out-Null
    } | Out-Null
}

# --------------------------------------------------------------------------
# build-all
# --------------------------------------------------------------------------

function Invoke-RoboBuildAll {
    <#
      The authoritative complete build. Every stage is a recorded step, so a
      failure names the stage that failed and the command exits non-zero.
    #>
    param([string]$EspFlags = "")

    Write-RoboSection "RoboticelaOS build"
    Write-RoboKeyValue "Target" (("{0} / {1}" -f $script:RoboConfig.TargetArch, $script:RoboConfig.RuntimeConfig))
    Write-RoboKeyValue "Output" $script:RoboOut.Build

    Invoke-RoboBuildCheck

    # Order is the real dependency order, not the declaration order of the
    # commands. The runtime's own build treats libc.a, crt0/crti/crtn and the
    # linker script as required *inputs*, so the bare-metal userland step (which
    # produces them) has to run before the runtime build, not after it.
    Invoke-RoboBuildAssets
    Invoke-RoboBuildUserlandCommand
    Invoke-RoboBuildRuntimeCommand
    Invoke-RoboBuildBootloader
    Invoke-RoboBuildBootloaderTests
    Invoke-RoboBuildKernelCommand
    Invoke-RoboBuildImageCommand -Flags $EspFlags
    Invoke-RoboBuildArtifactsCommand

    Show-RoboBuildOutput
}

function Show-RoboBuildOutput {
    $tree = Get-RoboDeploymentTree
    Write-RoboSection "Flash drive"
    Write-RoboKeyValue "Copy this folder" $tree
    Write-RoboInfo "format a USB stick as FAT32, then copy the CONTENTS of that folder onto it"
    Write-RoboInfo "the stick root must be EFI\BOOT\BOOTX64.EFI — do not nest an extra folder"
    Write-RoboInfo "disable Secure Boot and boot the PC from USB in UEFI mode"
    Write-RoboBlank
    Write-RoboKeyValue "Disk image" $script:RoboArtifact.DeployImage
    $img = Get-Item -LiteralPath $script:RoboArtifact.DeployImage -ErrorAction SilentlyContinue
    if ($img) { Write-RoboInfo ((Get-RoboFileSizeText $img.Length) + " — or flash it with 'scripts/robo flash --device <target> --yes'") }
    $zip = Join-Path $script:RoboOut.Images "roboticelaos-usb.zip"
    if (Test-Path -LiteralPath $zip) {
        Write-RoboKeyValue "USB archive" $zip
        Write-RoboInfo "unzip onto the FAT32 stick root if you prefer a single file"
    }
    Write-RoboBlank
    Write-RoboKeyValue "Artifacts" $script:RoboOut.Artifacts
    Write-RoboKeyValue "Manifest" $script:RoboArtifact.DeployManifest
    Write-RoboKeyValue "Logs" $script:RoboOut.Logs
}

function Invoke-RoboCleanCommand {
    param([switch]$IncludeSources)
    Write-RoboSection "Clean"
    Initialize-RoboOutputTree

    # Pipeline outputs.
    foreach ($dir in @($script:RoboOut.Build)) {
        if (Test-Path -LiteralPath $dir) {
            Write-RoboStep "CLEAN" $dir
            Remove-Item -LiteralPath $dir -Recurse -Force
        }
    }
    # Sub-project scratch directories. These are the sub-builds' own object
    # trees; removing them is what makes the next build a full rebuild, and
    # none of them contain authored files.
    foreach ($dir in @($script:RoboOut.Bootloader, $script:RoboOut.Kernel, $script:RoboOut.Libc, $script:RoboOut.Runtime)) {
        if (Test-Path -LiteralPath $dir) {
            Write-RoboStep "CLEAN" $dir
            Remove-Item -LiteralPath $dir -Recurse -Force
        }
    }
    # Files the build writes into the bootloader volume directory.
    foreach ($f in @("kernel.elf", "initrd", "recovery.elf", "install.elf")) {
        $p = Join-Path $script:RoboPaths.Bootloader "volumes\sys\$f"
        if (Test-Path -LiteralPath $p) {
            Remove-Item -LiteralPath $p -Force
            Write-RoboStep "CLEAN" $p
        }
    }
    if ($IncludeSources) {
        $baked = @()
        $baked += Get-ChildItem (Join-Path $script:RoboPaths.Bootloader "assets") -Filter *.rbim -File -ErrorAction SilentlyContinue
        $baked += Get-ChildItem (Join-Path $script:RoboPaths.Bootloader "assets\fonts") -Filter *.rbfn -File -ErrorAction SilentlyContinue
        foreach ($f in $baked) {
            Remove-Item -LiteralPath $f.FullName -Force
        }
        if ($baked.Count -gt 0) { Write-RoboStep "CLEAN" "baked assets and fonts ($($baked.Count) files)" }
    }
    Initialize-RoboOutputTree
    Write-RoboPass "clean"
}

function Invoke-RoboRebuildCommand {
    param([string]$EspFlags = "")
    Invoke-RoboCleanCommand
    $script:RoboFlag.Rebuild = $true
    Invoke-RoboBuildAll -EspFlags $EspFlags
}
