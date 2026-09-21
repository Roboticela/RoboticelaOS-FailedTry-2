function Get-RoboRepoRoot {
    param([string]$Start = $PSScriptRoot)
    $here = $Start
    while ($here) {
        if (Test-Path (Join-Path $here "AGENTS.md")) { return $here }
        $parent = Split-Path $here -Parent
        if ($parent -eq $here) { break }
        $here = $parent
    }
    throw "RoboticelaOS repository root not found from $Start"
}
