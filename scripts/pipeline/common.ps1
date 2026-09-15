# RoboticelaOS pipeline — shared infrastructure.
#
# Every command in the `robo` entry point goes through these helpers so that
# exit codes, logging, timeouts and result recording behave identically no
# matter which subsystem is being built or tested.
#
# Nothing here may print a pass that was not observed: the only function that
# reports success is Invoke-RoboStep, and only when its script block returned
# without throwing and every command it ran exited zero.

# --------------------------------------------------------------------------
# Console output
# --------------------------------------------------------------------------

$script:RoboUseColor = -not [bool]$env:NO_COLOR
$script:RoboSectionDepth = 0

function Write-RoboBlank { Write-Host "" }

function Write-RoboRule {
    param([int]$Width = 68)
    Write-Host ("  " + ("-" * $Width)) -ForegroundColor DarkGray
}

function Write-RoboSection {
    param([Parameter(Mandatory)][string]$Title)
    if ($script:RoboSectionDepth -gt 0) { Write-RoboBlank }
    $script:RoboSectionDepth++
    Write-Host ""
    Write-Host "  $Title" -ForegroundColor White
    Write-RoboRule
}

#  STEP  message                       <- stage labels (BUILD, TEST, RUN, ...)
function Write-RoboStep {
    param(
        [Parameter(Mandatory)][string]$Stage,
        [Parameter(Mandatory)][string]$Message
    )
    $tag = $Stage.ToUpperInvariant().PadRight(7)
    Write-Host ("  " + $tag + " " + $Message) -ForegroundColor Gray
}

function Write-RoboInfo {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host ("          " + $Message) -ForegroundColor DarkGray
}

function Write-RoboWarn {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host ("  WARN    " + $Message) -ForegroundColor Yellow
}

function Write-RoboFail {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host ("  FAIL    " + $Message) -ForegroundColor Red
}

function Write-RoboPass {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host ("  PASS    " + $Message) -ForegroundColor Green
}

function Write-RoboSkipped {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host ("  SKIP    " + $Message) -ForegroundColor DarkYellow
}

function Write-RoboKeyValue {
    param(
        [Parameter(Mandatory)][string]$Key,
        [AllowEmptyString()][string]$Value
    )
    Write-Host ("  " + $Key.PadRight(22) + " " + $Value) -ForegroundColor Gray
}

function Write-RoboCommandLine {
    param([Parameter(Mandatory)][AllowEmptyCollection()][string[]]$Arguments)
    if (-not $script:RoboVerbose) { return }
    Write-RoboInfo ("> " + (Get-RoboArgLine $Arguments))
}

function Get-RoboArgLine {
    param([string[]]$Arguments)
    ($Arguments | ForEach-Object {
        $a = "$_"
        if ($a -match '[\s"]') { '"' + ($a -replace '"', '\"') + '"' } else { $a }
    }) -join ' '
}

# --------------------------------------------------------------------------
# Logging
# --------------------------------------------------------------------------

$script:RoboLogs = @{}

function New-RoboLog {
    <#
      Opens a log file under build/logs and returns a writer. All output is
      tee'd to disk; the pipeline never suppresses a process's output, it only
      decides whether to echo it to the console.
    #>
    param(
        [Parameter(Mandatory)][string]$Name,
        [switch]$Append
    )
    Initialize-RoboOutputTree
    $path = Join-Path $script:RoboOut.Logs ($Name + ".log")
    if (-not $Append -and (Test-Path -LiteralPath $path)) { Remove-Item -LiteralPath $path -Force }
    $script:RoboLogs[$Name] = $path
    return $path
}

function Add-RoboLog {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][AllowEmptyString()][string]$Text
    )
    if (-not $script:RoboLogs.ContainsKey($Name)) { return }
    $path = $script:RoboLogs[$Name]
    $dir = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    Add-Content -LiteralPath $path -Value $Text -Encoding utf8
}

function Get-RoboLogPath {
    param([Parameter(Mandatory)][string]$Name)
    return (Join-Path $script:RoboOut.Logs ($Name + ".log"))
}

# --------------------------------------------------------------------------
# Process execution
# --------------------------------------------------------------------------

function New-RoboProcessFile {
    param([string]$Name)
    Initialize-RoboOutputTree
    if ($Name) { return (New-RoboLog $Name) }
    return (Join-Path $script:RoboOut.Temp ("proc-" + [Guid]::NewGuid().ToString("N") + ".log"))
}

function New-RoboStreamPump {
    <#
      Wraps one redirected pipe so it can be drained incrementally. The stream
      is read through a pending Task, which is replaced each time a chunk
      arrives; the caller only ever checks `Task.Wait(ms)` and reads `Result`.

      This is what makes a 15-minute compile watchable at build/logs/*.log while
      still letting us enforce a timeout and read a trustworthy exit code.
      (Start-Process cannot do either: its -PassThru object leaves ExitCode
      empty on this PowerShell, and it cannot enforce a timeout.)
    #>
    param(
        [Parameter(Mandatory)]$Stream,
        [Parameter(Mandatory)][System.Text.Decoder]$Decoder
    )
    $buf = New-Object byte[] 65536
    $state = @{
        Stream  = $Stream
        Decoder = $Decoder
        Buf     = $buf
        Chars   = New-Object char[] 70000
        Task    = $Stream.ReadAsync($buf, 0, $buf.Length)
        Done    = $false
    }
    return $state
}

function Invoke-RoboProcess {
    <#
      Runs an executable.

      In capture mode (the default) stdout and stderr are streamed into a file
      as they arrive, so long builds are watchable while they run instead of
      only afterwards. With a -LogName the file is build/logs/<name>.log.

      With -Echo the child writes straight to the console: that is what the
      interactive commands (host preview, demo window, visible QEMU) want, and
      it keeps their output live.

      Returns:
        ExitCode  - the real exit code (-1 when the process never finished)
        TimedOut  - $true when the timeout fired and the process was killed
        Output    - captured text (empty in -Echo mode)
        Seconds   - wall time
        Log       - path of the named log, when one was requested

      -TimeoutSeconds <= 0 disables the timeout. A timeout is always reported
      as a failure; it is never treated as success.
    #>
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = "",
        [int]$TimeoutSeconds = 0,
        [string]$LogName = "",
        [switch]$Echo
    )

    $argLine = Get-RoboArgLine $Arguments
    Write-RoboCommandLine @(($FilePath) + $Arguments)

    Initialize-RoboOutputTree
    $logPath = New-RoboProcessFile -Name $LogName
    if (Test-Path -LiteralPath $logPath) { Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue }
    $header = "# " + $FilePath + " " + $argLine + [Environment]::NewLine
    if ($WorkingDirectory) { $header += "# cwd " + $WorkingDirectory + [Environment]::NewLine }
    $header += "# started " + (Get-Date).ToString("o") + [Environment]::NewLine
    [IO.File]::WriteAllText($logPath, $header)

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = $argLine
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    if ($WorkingDirectory) { $psi.WorkingDirectory = $WorkingDirectory }
    if (-not $Echo) {
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.StandardOutputEncoding = [System.Text.Encoding]::UTF8
        $psi.StandardErrorEncoding = [System.Text.Encoding]::UTF8
    }

    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $started = $false
    try { $started = $proc.Start() } catch { $started = $false }
    if (-not $started) {
        $msg = "failed to start $FilePath"
        [IO.File]::AppendAllText($logPath, $msg + [Environment]::NewLine)
        Write-RoboWarn $msg
        return @{ ExitCode = -1; TimedOut = $false; Output = $msg; Seconds = 0.0; Log = $logPath }
    }

    $deadline = if ($TimeoutSeconds -gt 0) { (Get-Date).AddSeconds($TimeoutSeconds) } else { [DateTime]::MaxValue }
    $timedOut = $false
    $sb = New-Object System.Text.StringBuilder

    if ($Echo) {
        while (-not $proc.WaitForExit(250)) {
            if ((Get-Date) -gt $deadline) {
                $timedOut = $true
                try { $proc.Kill() } catch {}
                try { $proc.WaitForExit(5000) | Out-Null } catch {}
                break
            }
        }
    } else {
        $writer = New-Object System.IO.StreamWriter($logPath, $true, [System.Text.Encoding]::UTF8)
        $writer.AutoFlush = $true
        $utf8 = [System.Text.Encoding]::UTF8
        $pumps = @(
            (New-RoboStreamPump -Stream $proc.StandardOutput.BaseStream -Decoder $utf8.GetDecoder()),
            (New-RoboStreamPump -Stream $proc.StandardError.BaseStream -Decoder $utf8.GetDecoder())
        )
        try {
            while ($true) {
                if ($pumps[0].Done -and $pumps[1].Done) { break }
                $didWork = $false
                foreach ($p in $pumps) {
                    if ($p.Done) { continue }
                    $ready = $false
                    try { $ready = $p.Task.Wait(50) } catch { $ready = $false }
                    if (-not $ready) { continue }
                    $didWork = $true
                    $n = 0
                    try { $n = $p.Task.Result } catch { $n = -1 }
                    if ($n -le 0) { $p.Done = $true; continue }
                    $cn = $p.Decoder.GetChars($p.Buf, 0, $n, $p.Chars, 0)
                    if ($cn -gt 0) {
                        $chunk = New-Object string($p.Chars, 0, $cn)
                        [void]$sb.Append($chunk)
                        $writer.Write($chunk)
                    }
                    try { $p.Task = $p.Stream.ReadAsync($p.Buf, 0, $p.Buf.Length) }
                    catch { $p.Done = $true }
                }
                if (-not $didWork -and (Get-Date) -gt $deadline) {
                    $timedOut = $true
                    try { $proc.Kill() } catch {}
                    break
                }
            }
        } finally {
            try { $proc.WaitForExit(5000) | Out-Null } catch {}
            try { $writer.Flush(); $writer.Dispose() } catch {}
        }
    }
    $sw.Stop()

    $exit = -1
    if (-not $timedOut) {
        try { $exit = $proc.ExitCode } catch { $exit = -1 }
    }

    $text = ""
    if (-not $Echo) {
        $note = "# exit " + $exit
        if ($timedOut) { $note += " (timeout after ${TimeoutSeconds}s)" }
        try { [IO.File]::AppendAllText($logPath, $note + [Environment]::NewLine) } catch {}
        $text = $sb.ToString()
        if (-not $LogName) { Remove-Item -LiteralPath $logPath -Force -ErrorAction SilentlyContinue }
    }

    return @{
        ExitCode = $exit
        TimedOut = $timedOut
        Output   = $text
        Seconds  = [Math]::Round($sw.Elapsed.TotalSeconds, 2)
        Log      = $(if ($LogName) { $logPath } else { "" })
    }
}

function Invoke-RoboScript {
    <#
      Runs a PowerShell script file in the same session (so it can dot-source
      the engines). Exit code 0 unless the script throws.
    #>
    param(
        [Parameter(Mandatory)][string]$Path,
        [hashtable]$Parameters = @{}
    )
    if (-not (Test-Path -LiteralPath $Path)) { throw "script not found: $Path" }
    & $Path @Parameters
    if (-not $?) { throw "script failed: $Path" }
}

# --------------------------------------------------------------------------
# Tool resolution and version reporting
# --------------------------------------------------------------------------

function Resolve-RoboTool {
    <#
      Resolution order: explicit override, PATH, then a known install hint.
      Returns $null when unavailable so callers can report a real "missing"
      instead of throwing from deep inside a build.
    #>
    param(
        [Parameter(Mandatory)][string]$Name,
        [string]$Override = "",
        [string]$Hint = ""
    )
    if ($Override -and (Test-Path -LiteralPath $Override)) { return (Resolve-Path -LiteralPath $Override).Path }
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd -and $cmd.Source) { return $cmd.Source }
    if ($Hint) {
        $p = Join-Path $Hint $Name
        if (Test-Path -LiteralPath $p) { return (Resolve-Path -LiteralPath $p).Path }
        if (-not $Name.EndsWith(".exe")) {
            $pe = Join-Path $Hint ($Name + ".exe")
            if (Test-Path -LiteralPath $pe) { return (Resolve-Path -LiteralPath $pe).Path }
        }
    }
    return $null
}

function Get-RoboToolVersion {
    param(
        [Parameter(Mandatory)][string]$Path,
        [string]$Argument = "--version"
    )
    $r = Invoke-RoboProcess -FilePath $Path -Arguments @($Argument) -TimeoutSeconds 20
    $text = ($r.Output -split "`r?`n" | Where-Object { $_.Trim() } | Select-Object -First 1)
    if (-not $text) { return "unknown" }
    return $text.Trim()
}

# Known-required toolchain, resolved once per session.
$script:RoboTool = @{}

function Initialize-RoboTools {
    if ($script:RoboTool.Count -gt 0) { return }
    $script:RoboTool = @{
        clang      = Resolve-RoboTool "clang" $script:RoboToolOverride.clang $script:RoboLlvmHint
        lld_link   = Resolve-RoboTool "lld-link" $script:RoboToolOverride.lld_link $script:RoboLlvmHint
        ld_lld     = Resolve-RoboTool "ld.lld" $script:RoboToolOverride.ld_lld $script:RoboLlvmHint
        llvm_ar    = Resolve-RoboTool "llvm-ar" $script:RoboToolOverride.llvm_ar $script:RoboLlvmHint
        llvm_readobj = Resolve-RoboTool "llvm-readobj" $script:RoboToolOverride.llvm_readobj $script:RoboLlvmHint
        python     = Resolve-RoboTool "python" $script:RoboToolOverride.python ""
        qemu       = Resolve-RoboTool "qemu-system-x86_64" $script:RoboToolOverride.qemu (Join-Path ${env:ProgramFiles} "qemu")
    }
    if (-not $script:RoboTool.python) { $script:RoboTool.python = Resolve-RoboTool "python3" "" "" }
    if (-not $script:RoboTool.python) { $script:RoboTool.python = Resolve-RoboTool "py" "" "" }
}

function Get-RoboTool {
    param([Parameter(Mandatory)][string]$Name)
    Initialize-RoboTools
    if (-not $script:RoboTool.ContainsKey($Name)) { return $null }
    return $script:RoboTool[$Name]
}

function Assert-RoboTool {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Why
    )
    $t = Get-RoboTool $Name
    if (-not $t) {
        throw ("required tool '$Name' is missing ($Why). Run 'scripts/robo check' for details, " +
               "or set the matching ROBO_* override.")
    }
    return $t
}

# --------------------------------------------------------------------------
# Filesystem assertions
# --------------------------------------------------------------------------

function Assert-RoboFile {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$What
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$What missing: $Path" }
    $info = Get-Item -LiteralPath $Path
    if ($info.Length -le 0) { throw "$What is empty: $Path" }
    return $info
}

function Get-RoboFileSizeText {
    param([Parameter(Mandatory)][long]$Bytes)
    if ($Bytes -ge 1MB) { return ("{0:N1} MB" -f ($Bytes / 1MB)) }
    if ($Bytes -ge 1KB) { return ("{0:N1} KB" -f ($Bytes / 1KB)) }
    return "$Bytes B"
}

# --------------------------------------------------------------------------
# Test/build result recording
# --------------------------------------------------------------------------

$script:RoboResults = @{
    Started = $null
    Suites  = $null
}
$script:RoboFailed = $false

function Reset-RoboResults {
    $script:RoboResults.Started = (Get-Date)
    $script:RoboResults.Suites = New-Object System.Collections.ArrayList
    $script:RoboFailed = $false
}

function Add-RoboResult {
    <#
      Records one observable outcome. Status is exactly one of:
        pass | fail | skip
      Anything that did not actually run must be recorded as skip, never pass.
    #>
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][ValidateSet("pass", "fail", "skip")][string]$Status,
        [double]$Seconds = 0,
        [string]$Detail = "",
        [string]$Log = "",
        [string]$Layer = ""
    )
    if ($null -eq $script:RoboResults.Suites) { Reset-RoboResults }
    $null = $script:RoboResults.Suites.Add([ordered]@{
        name    = $Name
        status  = $Status
        layer   = $Layer
        seconds = [Math]::Round($Seconds, 2)
        detail  = $Detail
        log     = $Log
    })
    if ($Status -eq "fail") { $script:RoboFailed = $true }
}

function Save-RoboResults {
    <#
      Writes a machine-readable summary (JSON) plus a JUnit XML file so CI can
      consume the same run the developer watched.
    #>
    param([Parameter(Mandatory)][string]$Name)

    Initialize-RoboOutputTree
    $started = if ($script:RoboResults.Started) { $script:RoboResults.Started } else { Get-Date }
    $suites = @($script:RoboResults.Suites)
    $pass = @($suites | Where-Object { $_.status -eq "pass" }).Count
    $fail = @($suites | Where-Object { $_.status -eq "fail" }).Count
    $skip = @($suites | Where-Object { $_.status -eq "skip" }).Count
    $elapsed = [Math]::Round(((Get-Date) - $started).TotalSeconds, 2)

    $summary = [ordered]@{
        name       = $Name
        started    = $started.ToString("o")
        seconds    = $elapsed
        total      = $suites.Count
        passed     = $pass
        failed     = $fail
        skipped    = $skip
        ok         = ($fail -eq 0)
        suites     = $suites
    }

    $jsonPath = Join-Path $script:RoboOut.Results ($Name + ".json")
    $summary | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $jsonPath -Encoding utf8

    $xml = New-Object System.Text.StringBuilder
    [void]$xml.AppendLine('<?xml version="1.0" encoding="UTF-8"?>')
    [void]$xml.AppendLine(('<testsuites name="{0}" tests="{1}" failures="{2}" skipped="{3}" time="{4}">' -f
            [Security.SecurityElement]::Escape($Name), $suites.Count, $fail, $skip,
            $elapsed.ToString([Globalization.CultureInfo]::InvariantCulture)))
    foreach ($s in $suites) {
        $t = $s.seconds.ToString([Globalization.CultureInfo]::InvariantCulture)
        [void]$xml.AppendLine(('  <testsuite name="{0}" tests="1" failures="{1}" skipped="{2}" time="{3}">' -f
                [Security.SecurityElement]::Escape($s.name),
                $(if ($s.status -eq "fail") { 1 } else { 0 }),
                $(if ($s.status -eq "skip") { 1 } else { 0 }), $t))
        [void]$xml.AppendLine(('    <testcase name="{0}" classname="{1}" time="{2}">' -f
                [Security.SecurityElement]::Escape($s.name),
                [Security.SecurityElement]::Escape($s.layer), $t))
        if ($s.status -eq "fail") {
            [void]$xml.AppendLine(('      <failure message="{0}">{1}</failure>' -f
                    [Security.SecurityElement]::Escape($s.detail),
                    [Security.SecurityElement]::Escape($s.detail)))
        } elseif ($s.status -eq "skip") {
            [void]$xml.AppendLine(('      <skipped message="{0}" />' -f [Security.SecurityElement]::Escape($s.detail)))
        }
        [void]$xml.AppendLine('    </testcase>')
        [void]$xml.AppendLine('  </testsuite>')
    }
    [void]$xml.AppendLine('</testsuites>')
    $xmlPath = Join-Path $script:RoboOut.Results ($Name + ".junit.xml")
    $xml.ToString() | Set-Content -LiteralPath $xmlPath -Encoding utf8

    return @{ Path = $jsonPath; Junit = $xmlPath; Passed = $pass; Failed = $fail; Skipped = $skip }
}

function Show-RoboResultSummary {
    param([Parameter(Mandatory)][string]$Name)
    $suites = @($script:RoboResults.Suites)
    $pass = @($suites | Where-Object { $_.status -eq "pass" }).Count
    $fail = @($suites | Where-Object { $_.status -eq "fail" }).Count
    $skip = @($suites | Where-Object { $_.status -eq "skip" }).Count

    Write-RoboSection "Summary"
    if ($fail -gt 0) {
        foreach ($s in ($suites | Where-Object { $_.status -eq "fail" })) {
            Write-RoboFail ("{0}  {1}" -f $s.name, $s.detail)
        }
    }
    Write-RoboKeyValue "Passed" $pass
    Write-RoboKeyValue "Failed" $fail
    Write-RoboKeyValue "Skipped" $skip
    $saved = Save-RoboResults -Name $Name
    Write-RoboKeyValue "Results" $saved.Path
    Write-RoboKeyValue "JUnit" $saved.Junit
}

# --------------------------------------------------------------------------
# Step execution
# --------------------------------------------------------------------------

function Invoke-RoboStep {
    <#
      Runs one observable step. On success it records `pass`, on failure it
      records `fail` and prints the captured output. With -Fatal (the default)
      the error is rethrown so the command exits non-zero; with -Continue the
      pipeline keeps going (used only by test-all, which must run everything
      and then fail).
    #>
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Layer,
        [Parameter(Mandatory)][scriptblock]$Action,
        [switch]$Fatal,
        [switch]$Continue,
        [switch]$NoSummary
    )
    $fatal = -not $Continue
    if ($PSBoundParameters.ContainsKey("Fatal")) { $fatal = [bool]$Fatal }
    if ($PSBoundParameters.ContainsKey("Continue")) { $fatal = -not [bool]$Continue }

    Write-RoboStep $Layer $Name
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        & $Action
        $sw.Stop()
        $secs = [Math]::Round($sw.Elapsed.TotalSeconds, 2)
        Write-RoboPass ("{0}  ({1:N2}s)" -f $Name, $secs)
        Add-RoboResult -Name $Name -Status pass -Seconds $secs -Layer $Layer
        return $true
    } catch {
        $sw.Stop()
        $secs = [Math]::Round($sw.Elapsed.TotalSeconds, 2)
        $msg = "$_"
        Write-RoboFail ("{0}  ({1:N2}s)" -f $Name, $secs)
        foreach ($line in ($msg -split "`r?`n")) {
            if ($line.Trim()) { Write-RoboInfo $line.Trim() }
        }
        Add-RoboResult -Name $Name -Status fail -Seconds $secs -Layer $Layer -Detail $msg
        if ($fatal) { throw }
        return $false
    }
}

function Invoke-RoboRecordedCommand {
    <#
      Runs a real process as a recorded step: a non-zero exit code or a
      timeout is a failure, and the child's output is echoed only on failure
      (or when -Echo is given).
    #>
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Layer,
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = "",
        [int]$TimeoutSeconds = 0,
        [string]$LogName = "",
        [switch]$Continue,
        [switch]$Echo,
        [switch]$AllowFailure
    )
    $ok = Invoke-RoboStep -Name $Name -Layer $Layer -Continue:$Continue -Action {
        $r = Invoke-RoboProcess -FilePath $FilePath -Arguments $Arguments `
            -WorkingDirectory $WorkingDirectory -TimeoutSeconds $TimeoutSeconds -LogName $LogName
        if ($r.TimedOut) {
            throw ("timed out after ${TimeoutSeconds}s. Last output:`n" + (Get-RoboTail $r.Output 40))
        }
        if ($r.ExitCode -ne 0 -and -not $AllowFailure) {
            $logNote = if ($LogName) { " (full log: $(Get-RoboLogPath $LogName))" } else { "" }
            throw ("exited with code $($r.ExitCode)$logNote`n" + (Get-RoboTail $r.Output 60))
        }
        if ($Echo -and $r.Output) { Write-Host $r.Output }
        if ($r.ExitCode -ne 0 -and $AllowFailure) {
            Write-RoboWarn ("exit code $($r.ExitCode) (allowed)")
        }
    }
    return $ok
}

function Get-RoboTail {
    param(
        [AllowEmptyString()][string]$Text,
        [int]$Lines = 40
    )
    if (-not $Text) { return "(no output)" }
    $all = @($Text -split "`r?`n" | Where-Object { $_.Trim() })
    if ($all.Count -le $Lines) { return ($all -join "`n") }
    return (@("... " + ($all.Count - $Lines) + " earlier lines omitted ...") + ($all[($all.Count - $Lines)..($all.Count - 1)]) -join "`n")
}

# --------------------------------------------------------------------------
# Time
# --------------------------------------------------------------------------

function Format-RoboDuration {
    param([Parameter(Mandatory)][double]$Seconds)
    if ($Seconds -ge 60) {
        $m = [Math]::Floor($Seconds / 60)
        $s = [Math]::Round($Seconds - ($m * 60), 1)
        return ("{0}m {1}s" -f $m, $s)
    }
    return ("{0:N2}s" -f $Seconds)
}
