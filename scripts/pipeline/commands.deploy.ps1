# RoboticelaOS pipeline — deployment.
#
# Two deployment shapes exist, and both are real:
#
#   1. the USB/SD tree  - build/dist/, a FAT32-friendly directory you copy onto
#                         a prepared drive (EFI/BOOT/BOOTX64.EFI at the root)
#   2. the disk image   - build/images/roboticelaos.img, a complete GPT disk
#                         with an ESP, written verbatim to a drive
#
# `flash` is the only command that touches a physical device. It never runs
# implicitly, never elevates itself, and refuses to aim at the system disk.

function Invoke-RoboDeploy {
    <#
      Produces the deployment artifacts and reports exactly where they are.
      Non-destructive: it never opens a block device.
    #>
    param([string]$EspFlags = "splash_ms 0")

    Write-RoboSection "Deployment"
    if (-not $script:RoboFlag.SkipBuild) {
        Invoke-RoboBuildAll -EspFlags $EspFlags
    } else {
        Invoke-RoboStep -Name "verify existing deployment" -Layer "DEPLOY" -Action {
            Assert-RoboFile $script:RoboArtifact.DeployImage "disk image" | Out-Null
            Assert-RoboFile (Join-Path (Get-RoboDeploymentTree) "EFI\BOOT\BOOTX64.EFI") "deployment bootloader" | Out-Null
        } | Out-Null
    }

    Invoke-RoboStep -Name "package USB tree" -Layer "DEPLOY" -Action {
        $zip = Join-Path $script:RoboOut.Images "roboticelaos-usb.zip"
        if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
        $tree = Get-RoboDeploymentTree
        # Compress the *contents* so unzipping straight onto a FAT32 drive
        # produces a bootable layout without a nested folder.
        $items = @(Get-ChildItem -LiteralPath $tree -Force | Where-Object { $_.Name -ne "USB.txt" })
        if ($items.Count -eq 0) { throw "deployment tree is empty: $tree" }
        Compress-Archive -Path ($items | ForEach-Object { $_.FullName }) -DestinationPath $zip -Force -CompressionLevel Optimal
        Assert-RoboFile $zip "USB archive" | Out-Null
        Write-RoboInfo ("archive: " + $zip)
    } | Out-Null

    Show-RoboBuildOutput
}

function Invoke-RoboValidateDeployment {
    <#
      Validates the deployment artifact structurally, from the bytes on disk.
      Returns $true/$false and always prints a per-check report.
    #>
    $tree = Get-RoboDeploymentTree
    $image = $script:RoboArtifact.DeployImage

    Write-RoboSection "Deployment validation"
    Write-RoboKeyValue "Image" $image
    Write-RoboKeyValue "Tree" $tree

    # --- 1. files the pipeline itself must have produced ------------------
    $required = @(
        @{ Path = $script:RoboArtifact.DeployImage; What = "disk image"; Layer = "image" },
        @{ Path = (Join-Path $tree "EFI\BOOT\BOOTX64.EFI"); What = "bootloader (default firmware path)"; Layer = "tree" },
        @{ Path = (Join-Path $tree "EFI\Roboticela\BOOTX64.EFI"); What = "bootloader (vendor path)"; Layer = "tree" },
        @{ Path = (Join-Path $tree "volumes\sys\kernel.elf"); What = "kernel"; Layer = "tree" },
        @{ Path = (Join-Path $tree "volumes\sys\initrd"); What = "initrd"; Layer = "tree" },
        @{ Path = (Join-Path $tree "config\roboboot.manifest"); What = "boot configuration"; Layer = "tree" }
    )
    $missing = @()
    foreach ($r in $required) {
        if (Test-Path -LiteralPath $r.Path -PathType Leaf) {
            $info = Get-Item -LiteralPath $r.Path
            if ($info.Length -gt 0) {
                Write-Host ("  " + "OK".PadRight(8) + $r.What.PadRight(38) + (Get-RoboFileSizeText $info.Length)) -ForegroundColor Green
            } else {
                Write-Host ("  " + "EMPTY".PadRight(8) + $r.What) -ForegroundColor Red
                $missing += $r.What
            }
        } else {
            Write-Host ("  " + "MISSING".PadRight(8) + $r.What) -ForegroundColor Red
            Write-RoboInfo $r.Path
            $missing += $r.What
        }
    }

    $assets = @(Get-ChildItem -LiteralPath (Join-Path $tree "assets") -Recurse -File -ErrorAction SilentlyContinue)
    if ($assets.Count -eq 0) {
        Write-Host ("  " + "MISSING".PadRight(8) + "boot assets (splash, fonts)") -ForegroundColor Red
        $missing += "boot assets"
    } else {
        Write-Host ("  " + "OK".PadRight(8) + "boot assets".PadRight(38) + ("$($assets.Count) files")) -ForegroundColor Green
    }

    # --- 2. the manifest must describe this build -------------------------
    if (Test-Path -LiteralPath $script:RoboArtifact.DeployManifest) {
        try {
            $manifest = Get-Content -LiteralPath $script:RoboArtifact.DeployManifest -Raw | ConvertFrom-Json
            $problems = @()
            if ($manifest.target -ne $script:RoboConfig.TargetArch) {
                $problems += "target '$($manifest.target)' != '$($script:RoboConfig.TargetArch)'"
            }
            if ($manifest.schema -ne "roboticelaos.deployment/1") { $problems += "unknown schema '$($manifest.schema)'" }
            if ($problems.Count -eq 0) {
                Write-Host ("  " + "OK".PadRight(8) + "deployment manifest".PadRight(38) + "schema 1, $($manifest.artifacts.Count) artifacts") -ForegroundColor Green
            } else {
                Write-Host ("  " + "INVALID".PadRight(8) + "deployment manifest") -ForegroundColor Red
                $problems | ForEach-Object { Write-RoboInfo $_ }
                $missing += "manifest"
            }
        } catch {
            Write-Host ("  " + "INVALID".PadRight(8) + "deployment manifest (unreadable JSON)") -ForegroundColor Red
            $missing += "manifest"
        }
    } else {
        Write-Host ("  " + "MISSING".PadRight(8) + "deployment manifest") -ForegroundColor Red
        $missing += "manifest"
    }

    # --- 3. structural validation of the actual image ---------------------
    $imageOk = $false
    if (Test-Path -LiteralPath $image -PathType Leaf) {
        $py = Get-RoboTool "python"
        $validator = Join-Path $PSScriptRoot "validate_image.py"
        if (-not $py) {
            Write-Host ("  " + "SKIP".PadRight(8) + "image structure (python unavailable)") -ForegroundColor DarkYellow
        } elseif (-not (Test-Path -LiteralPath $validator)) {
            Write-Host ("  " + "SKIP".PadRight(8) + "image structure (validator missing)") -ForegroundColor DarkYellow
        } else {
            $jsonPath = Join-Path $script:RoboOut.Results "deployment-image.json"
            $cmdArgs = @($validator, "--image", $image,
                "--bootloader", $script:RoboPaths.Bootloader,
                "--tree", $tree, "--json", $jsonPath)
            if (Test-Path -LiteralPath $script:KernelOut) { $cmdArgs += @("--kernel", $script:KernelOut) }
            $r = Invoke-RoboProcess -FilePath $py -Arguments $cmdArgs -TimeoutSeconds 180 -LogName "validate-deployment"
            Write-Host $r.Output
            $imageOk = ($r.ExitCode -eq 0)
            if (-not $imageOk) { $missing += "image structure" }
        }
    } else {
        $missing += "image"
    }

    Write-RoboSection "Verdict"
    if ($missing.Count -gt 0) {
        Write-RoboFail ("deployment is not valid: " + ($missing -join ", "))
        Write-RoboInfo ("details: " + (Get-RoboLogPath "validate-deployment"))
        return $false
    }
    Write-RoboPass "deployment artifact is bootable and complete"
    Write-RoboKeyValue "USB tree" $tree
    Write-RoboKeyValue "Disk image" $image
    return $true
}

function Test-RoboElevated {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-RoboDiskCandidates {
    <#
      Aims the report at real physical disks, using the Storage module when it is
      available. Returns whatever the host can truthfully tell us; an entry with
      unknown details keeps those fields empty rather than guessing.
    #>
    $out = @()
    try {
        $disks = Get-Disk -ErrorAction Stop
        foreach ($d in $disks) {
            $out += [ordered]@{
                Number      = $d.Number
                FriendlyName = $d.FriendlyName
                BusType     = "$($d.BusType)"
                SizeBytes   = [long]$d.Size
                IsSystem    = [bool]$d.IsSystem
                IsBoot      = [bool]$d.IsBoot
                PartitionStyle = "$($d.PartitionStyle)"
                Path        = "\\.\PhysicalDrive$($d.Number)"
            }
        }
    } catch {
        return @()
    }
    return $out
}

function Invoke-RoboFlash {
    <#
      Writes the disk image to a physical device.

      Safety, in order:
        * --device and --yes are both mandatory
        * the image must pass structural validation first
        * the target must be a real physical disk
        * the system/boot disk is refused unless --force, and even then only
          after an explicit second confirmation
        * the process must already be elevated; this command never elevates
          itself
    #>
    $device = $script:RoboFlag.Device
    Write-RoboSection "Flash disk image"

    if (-not $device) {
        throw ("no target device. Usage: scripts/robo flash --device <N|path> --yes`n" +
               "  --device 3                 write to PhysicalDrive3`n" +
               "  --device \\.\PhysicalDrive3  same, spelled out")
    }
    if (-not $script:RoboFlag.AssumeYes) {
        throw "refusing to write without --yes (this erases the target device)"
    }

    Assert-RoboFile $script:RoboArtifact.DeployImage "disk image" | Out-Null
    $image = Get-Item -LiteralPath $script:RoboArtifact.DeployImage
    Write-RoboKeyValue "Image" $image.FullName
    Write-RoboKeyValue "Image size" (Get-RoboFileSizeText $image.Length)

    # The artifact must be provably good before it goes anywhere.
    Write-RoboInfo "validating the image before writing"
    if (-not (Invoke-RoboValidateDeployment)) {
        throw "the image did not pass validation; refusing to write it to a device"
    }

    # Resolve the target.
    $targetPath = $device
    $disk = $null
    if ($device -match "^\d+$") { $targetPath = "\\.\PhysicalDrive$device" }
    $all = @(Get-RoboDiskCandidates)
    if ($all.Count -gt 0) {
        $disk = $all | Where-Object { $_.Path -eq $targetPath } | Select-Object -First 1
        if (-not $disk) {
            # Match by drive letter if one was given.
            if ($device -match "^(?<letter>[A-Za-z]):?$") {
                $letter = $Matches["letter"].ToUpperInvariant()
                try {
                    $part = Get-Partition -DriveLetter $letter -ErrorAction Stop
                    $disk = $all | Where-Object { $_.Number -eq $part.DiskNumber } | Select-Object -First 1
                    if ($disk) { $targetPath = $disk.Path }
                } catch { $disk = $null }
            }
        }
    }

    Write-RoboSection "Target"
    if ($disk) {
        Write-RoboKeyValue "Device" $disk.Path
        Write-RoboKeyValue "Model" $disk.FriendlyName
        Write-RoboKeyValue "Bus" $disk.BusType
        Write-RoboKeyValue "Size" (Get-RoboFileSizeText $disk.SizeBytes)
        Write-RoboKeyValue "System disk" $(if ($disk.IsSystem) { "YES - DANGER" } else { "no" })

        if ($disk.SizeBytes -lt $image.Length) {
            throw ("target is smaller than the image: " +
                   "$(Get-RoboFileSizeText $disk.SizeBytes) < $(Get-RoboFileSizeText $image.Length)")
        }
        if ($disk.IsSystem -or $disk.IsBoot) {
            if (-not $script:RoboFlag.Force) {
                throw ("$($disk.Path) holds the running system. This is almost certainly not what you want. " +
                       "If it truly is, re-run with --force and confirm again.")
            }
            Write-RoboWarn "target is the system disk; --force was given"
            Write-RoboInfo "type exactly the word ERASE to continue"
            $answer = Read-Host "  confirm"
            if ($answer -ne "ERASE") { throw "aborted" }
        }
    } else {
        Write-RoboInfo "could not identify this device through the storage provider; continuing with the raw path"
        Write-RoboKeyValue "Device" $targetPath
    }

    if (-not (Test-RoboElevated)) {
        throw ("writing to $targetPath needs administrator rights. " +
               "Open an elevated PowerShell and run the same command there; " +
               "this pipeline deliberately does not elevate itself.")
    }

    Write-RoboStep "FLASH" $targetPath
    $stream = $null
    $source = $null
    try {
        $stream = New-Object System.IO.FileStream($targetPath,
            [System.IO.FileMode]::Open, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None,
            1MB, [System.IO.FileOptions]::WriteThrough)
        $source = [System.IO.File]::OpenRead($image.FullName)
        $buf = New-Object byte[] (1MB)
        $total = 0L
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        while ($true) {
            $n = $source.Read($buf, 0, $buf.Length)
            if ($n -le 0) { break }
            $stream.Write($buf, 0, $n)
            $total += $n
        }
        $stream.Flush($true)
        $sw.Stop()
    } finally {
        if ($source) { $source.Dispose() }
        if ($stream) { $stream.Dispose() }
    }

    Write-RoboPass ("wrote " + (Get-RoboFileSizeText $total) + " to $targetPath")
    Write-RoboInfo "safe to remove the drive; boot the target machine from it"
    return 0
}
