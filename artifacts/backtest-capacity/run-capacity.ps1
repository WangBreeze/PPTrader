param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [int]$Symbols = 5207,
    [int]$Bars = 239,
    [int]$Concurrency = 20,
    [string]$TemplateSymbol = '600519'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildRoot = Join-Path $projectRoot 'out\build\vs2022-x64'
$executable = Join-Path $buildRoot "$Configuration\pptrader_backtest_capacity.exe"
$database = Join-Path $buildRoot 'Debug\data\pptrader.db'
if (-not (Test-Path -LiteralPath $executable)) { throw "Capacity probe not built: $executable" }
if (-not (Test-Path -LiteralPath $database)) { throw "Template database not found: $database" }

$stdoutPath = Join-Path $PSScriptRoot "capacity-$Symbols-$Bars-$Concurrency.stdout.json"
$stderrPath = Join-Path $PSScriptRoot "capacity-$Symbols-$Bars-$Concurrency.stderr.log"
$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $executable
$startInfo.Arguments = "--database `"$database`" --symbol $TemplateSymbol --symbols $Symbols --bars $Bars --concurrency $Concurrency"
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$process = New-Object System.Diagnostics.Process
$process.StartInfo = $startInfo
if (-not $process.Start()) { throw 'Capacity probe process did not start' }
$peakWorkingSet = 0L
$peakPrivateMemory = 0L
$sampleCount = 0
while (-not $process.HasExited) {
    Start-Sleep -Milliseconds 100
    $process.Refresh()
    $peakWorkingSet = [Math]::Max($peakWorkingSet, $process.WorkingSet64)
    $peakPrivateMemory = [Math]::Max($peakPrivateMemory, $process.PrivateMemorySize64)
    ++$sampleCount
}
$process.WaitForExit()
$stdout = $process.StandardOutput.ReadToEnd()
$stderr = $process.StandardError.ReadToEnd()
[System.IO.File]::WriteAllText($stdoutPath, $stdout)
[System.IO.File]::WriteAllText($stderrPath, $stderr)
if ($process.ExitCode -ne 0) {
    throw "Capacity probe failed with exit code $($process.ExitCode): $stderr"
}

$probe = $stdout | ConvertFrom-Json
$report = [ordered]@{
    configuration = $Configuration
    symbols = $probe.symbols
    barsPerSymbol = $probe.barsPerSymbol
    totalBars = $probe.totalBars
    concurrency = $probe.concurrency
    completed = $probe.completed
    failed = $probe.failed
    elapsedMs = $probe.elapsedMs
    symbolsPerSecond = $probe.symbolsPerSecond
    barsPerSecond = $probe.barsPerSecond
    taskP50Ms = $probe.taskP50Ms
    taskP95Ms = $probe.taskP95Ms
    heartbeatP95Ms = $probe.heartbeatP95Ms
    heartbeatMaxMs = $probe.heartbeatMaxMs
    peakWorkingSetMiB = [Math]::Round($peakWorkingSet / 1MB, 2)
    peakPrivateMemoryMiB = [Math]::Round($peakPrivateMemory / 1MB, 2)
    logicalProcessors = $probe.logicalProcessors
    dataSource = $probe.dataSource
    sampleCount = $sampleCount
}
$reportPath = Join-Path $PSScriptRoot "capacity-$Symbols-$Bars-$Concurrency.report.json"
$report | ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding utf8
$report | Format-List
