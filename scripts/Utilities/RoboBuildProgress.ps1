# Shared build-progress and resume helpers for Runtime and RoboBoot builds.
# Dot-source from Build.ps1 / roboboot-build-common.ps1 / BuildEverything.ps1.

if (-not $script:RoboBuildProgress) {
    $script:RoboBuildProgress = $null
}

function Initialize-RoboBuildProgress {
    param(
        [Parameter(Mandatory)][int]$Total,
        [int]$ToCompile = -1,
        [int]$Cached = -1,
        [string]$Label = "objects"
    )
    if ($ToCompile -lt 0) { $ToCompile = $Total }
    if ($Cached -lt 0) { $Cached = [Math]::Max(0, $Total - $ToCompile) }
    $script:RoboBuildProgress = [ordered]@{
        Label     = $Label
        Total     = $Total
        ToCompile = $ToCompile
        Cached    = $Cached
        Compiled  = 0
        Skipped   = 0
        Index     = 0
        Started   = Get-Date
    }
    Write-Host ("PLAN   {0}: {1} total | {2} up-to-date | {3} to compile" -f `
        $Label, $Total, $Cached, $ToCompile)
    if ($ToCompile -eq 0 -and $Total -gt 0) {
        Write-Host ("RESUME {0}: nothing stale - reusing existing objects" -f $Label)
    }
}

function Write-RoboBuildProgress {
    param(
        [switch]$Compiled,
        [switch]$Skipped,
        [string]$Src = ""
    )
    if (-not $script:RoboBuildProgress) { return }
    $p = $script:RoboBuildProgress
    if ($Compiled) { $p.Compiled++ }
    if ($Skipped) { $p.Skipped++ }
    $done = $p.Compiled + $p.Skipped
    $leftNeeded = [Math]::Max(0, $p.ToCompile - $p.Compiled)
    $name = if ($Src) { Split-Path $Src -Leaf } else { "" }
    if ($Compiled) {
        Write-Host ("CC     [{0}/{1}] {2}  (built {3}/{4} needed | left {5})" -f `
            $done, $p.Total, $name, $p.Compiled, $p.ToCompile, $leftNeeded)
    }
}

function Complete-RoboBuildProgress {
    if (-not $script:RoboBuildProgress) { return }
    $p = $script:RoboBuildProgress
    $elapsed = ((Get-Date) - [datetime]$p.Started).TotalSeconds
    Write-Host ("DONE   {0}: {1} compiled | {2} reused | {3} total ({4:N1}s)" -f `
        $p.Label, $p.Compiled, ($p.Total - $p.Compiled), $p.Total, $elapsed)
    $script:RoboBuildProgress = $null
}

function Get-RoboDepPath([string]$Obj) {
    return [System.IO.Path]::ChangeExtension($Obj, ".d")
}

function Get-RoboMakefileDeps([string]$DepFile) {
    <#
      Reads a clang -MMD makefile fragment and returns the input paths listed after the colon.
      Continued lines (backslash newline) are folded; paths with spaces escaped as backslash-space
      are restored.
    #>
    if (-not (Test-Path -LiteralPath $DepFile)) { return @() }
    $raw = Get-Content -LiteralPath $DepFile -Raw -ErrorAction SilentlyContinue
    if ([string]::IsNullOrWhiteSpace($raw)) { return @() }
    $folded = [regex]::Replace($raw, "\\\r?\n", " ")
    $colon = $folded.IndexOf(":")
    if ($colon -lt 0) { return @() }
    $tail = $folded.Substring($colon + 1).Trim()
    if (-not $tail) { return @() }
    $paths = [System.Collections.Generic.List[string]]::new()
    $current = New-Object System.Text.StringBuilder
    $chars = $tail.ToCharArray()
    for ($i = 0; $i -lt $chars.Length; $i++) {
        $ch = $chars[$i]
        if ($ch -eq [char]"\" -and ($i + 1) -lt $chars.Length -and $chars[$i + 1] -eq [char]" ") {
            [void]$current.Append(" ")
            $i++
            continue
        }
        if ([char]::IsWhiteSpace($ch)) {
            if ($current.Length -gt 0) {
                $paths.Add($current.ToString())
                [void]$current.Clear()
            }
            continue
        }
        [void]$current.Append($ch)
    }
    if ($current.Length -gt 0) {
        $paths.Add($current.ToString())
    }
    return @($paths | Where-Object { $_ -and $_ -ne "\" })
}

function Test-RoboObjectStale {
    param(
        [Parameter(Mandatory)][string]$Src,
        [Parameter(Mandatory)][string]$Obj,
        [datetime]$HeaderStamp = [datetime]::MinValue,
        [switch]$Force
    )
    if ($Force) { return $true }
    if (-not (Test-Path -LiteralPath $Obj)) { return $true }
    $objTime = (Get-Item -LiteralPath $Obj).LastWriteTimeUtc
    if ((Get-Item -LiteralPath $Src).LastWriteTimeUtc -gt $objTime) { return $true }

    $dep = Get-RoboDepPath $Obj
    $deps = Get-RoboMakefileDeps $dep
    if ($deps.Count -gt 0) {
        foreach ($d in $deps) {
            $path = $d
            if (-not [System.IO.Path]::IsPathRooted($path)) {
                $path = Join-Path (Split-Path $Obj -Parent) $d
            }
            if ((Test-Path -LiteralPath $path) -and
                ((Get-Item -LiteralPath $path).LastWriteTimeUtc -gt $objTime)) {
                return $true
            }
            $alt = $d -replace "/", "\"
            if ((Test-Path -LiteralPath $alt) -and
                ((Get-Item -LiteralPath $alt).LastWriteTimeUtc -gt $objTime)) {
                return $true
            }
        }
        return $false
    }

    <#
      No .d yet: only compare the source to the object. A global header stamp over-invalidates
      (one generated header such as DesktopPage.h would rebuild hundreds of unrelated objects).
      The next successful compile writes a .d, after which header-accurate resume works.
    #>
    return $false
}

function Get-RoboBuildEverythingStatePath([string]$RepoRoot) {
    return (Join-Path $RepoRoot "Build\.build-everything.json")
}

function Get-RoboBuildEverythingState([string]$RepoRoot, [string]$Configuration) {
    $path = Get-RoboBuildEverythingStatePath $RepoRoot
    if (-not (Test-Path -LiteralPath $path)) {
        return [pscustomobject]@{ configuration = $Configuration; completed = @() }
    }
    try {
        $state = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        if ($state.configuration -ne $Configuration) {
            return [pscustomobject]@{ configuration = $Configuration; completed = @() }
        }
        $completed = @()
        if ($state.completed) { $completed = @($state.completed) }
        return [pscustomobject]@{ configuration = $Configuration; completed = $completed }
    } catch {
        return [pscustomobject]@{ configuration = $Configuration; completed = @() }
    }
}

function Save-RoboBuildEverythingState([string]$RepoRoot, [string]$Configuration, [string[]]$Completed) {
    $dir = Join-Path $RepoRoot "Build"
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    $path = Get-RoboBuildEverythingStatePath $RepoRoot
    $payload = @{
        configuration = $Configuration
        completed     = @($Completed)
        updated       = (Get-Date).ToString("o")
    } | ConvertTo-Json -Depth 4
    Set-Content -LiteralPath $path -Value $payload -Encoding utf8
}

function Clear-RoboBuildEverythingState([string]$RepoRoot) {
    $path = Get-RoboBuildEverythingStatePath $RepoRoot
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
    }
}

# Pipeline build-all stage resume (run-qemu / scripts/robo build-all).
function Get-RoboBuildAllStatePath([string]$RepoRoot) {
    return (Join-Path $RepoRoot "Build\.build-all.json")
}

function Get-RoboBuildAllState([string]$RepoRoot, [string]$Configuration) {
    $path = Get-RoboBuildAllStatePath $RepoRoot
    if (-not (Test-Path -LiteralPath $path)) {
        return [pscustomobject]@{ configuration = $Configuration; completed = @() }
    }
    try {
        $state = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        if ($state.configuration -ne $Configuration) {
            return [pscustomobject]@{ configuration = $Configuration; completed = @() }
        }
        $completed = @()
        if ($state.completed) { $completed = @($state.completed) }
        return [pscustomobject]@{ configuration = $Configuration; completed = $completed }
    } catch {
        return [pscustomobject]@{ configuration = $Configuration; completed = @() }
    }
}

function Save-RoboBuildAllState([string]$RepoRoot, [string]$Configuration, [string[]]$Completed) {
    $dir = Join-Path $RepoRoot "Build"
    if (-not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    $path = Get-RoboBuildAllStatePath $RepoRoot
    $payload = @{
        configuration = $Configuration
        completed     = @($Completed)
        updated       = (Get-Date).ToString("o")
    } | ConvertTo-Json -Depth 4
    Set-Content -LiteralPath $path -Value $payload -Encoding utf8
}

function Clear-RoboBuildAllState([string]$RepoRoot) {
    $path = Get-RoboBuildAllStatePath $RepoRoot
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
    }
}
