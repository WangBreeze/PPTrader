param(
    [Parameter(Mandatory = $true)]
    [int]$TargetProcessId,
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [Parameter(Mandatory = $true)]
    [string]$DatabasePath,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory = $true)]
    [datetimeoffset]$EndAt,
    [int]$SampleSeconds = 30,
    [ValidateRange(0, 2147483647)]
    [int]$MinimumRuntimeSeconds = 0
)

$ErrorActionPreference = 'Stop'
$minimumSampleSeconds = 5
$effectiveSampleSeconds = [Math]::Max($minimumSampleSeconds, $SampleSeconds)
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Get-CanonicalPath([string]$Path, [string]$Description) {
    try {
        return [System.IO.Path]::GetFullPath((Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path)
    } catch {
        throw "$Description does not exist or cannot be resolved: $Path"
    }
}

function Test-SamePath([string]$Left, [string]$Right) {
    return [string]::Equals(
        $Left.TrimEnd('\'),
        $Right.TrimEnd('\'),
        [System.StringComparison]::OrdinalIgnoreCase)
}

function Assert-FixedLocalPath([string]$Path, [string]$Description) {
    if ($Path.StartsWith('\\')) {
        throw "$Description must be local, not a UNC path."
    }
    $root = [System.IO.Path]::GetPathRoot($Path)
    $drive = [System.IO.DriveInfo]::new($root)
    if ($drive.DriveType -ne [System.IO.DriveType]::Fixed) {
        throw "$Description must be on a fixed local drive; actual drive type is $($drive.DriveType)."
    }
}

function Get-FileSize([string]$Path) {
    if (-not [System.IO.File]::Exists($Path)) { return [int64]0 }
    return [int64]([System.IO.FileInfo]::new($Path).Length)
}

function Get-Percentile([object[]]$Values, [double]$Quantile) {
    if ($Values.Count -eq 0) { return [int64]0 }
    $index = [Math]::Min(
        $Values.Count - 1,
        [Math]::Max(0, [Math]::Ceiling($Values.Count * $Quantile) - 1))
    return [int64]$Values[$index]
}

function Get-SlopePerMinute([object[]]$Samples, [string]$Property) {
    if ($Samples.Count -lt 2) { return [double]0 }

    $origin = [datetimeoffset]$Samples[0].timestamp
    $points = @($Samples | ForEach-Object {
        [PSCustomObject]@{
            X = ([datetimeoffset]$_.timestamp - $origin).TotalSeconds
            Y = [double]($_.$Property)
        }
    })
    $meanX = [double](($points | Measure-Object -Property X -Average).Average)
    $meanY = [double](($points | Measure-Object -Property Y -Average).Average)
    [double]$numerator = 0
    [double]$denominator = 0
    foreach ($point in $points) {
        $dx = $point.X - $meanX
        $numerator += $dx * ($point.Y - $meanY)
        $denominator += $dx * $dx
    }
    if ($denominator -le 0) { return [double]0 }
    return [double](($numerator / $denominator) * 60.0)
}

function Get-MaximumSampleGapSeconds([object[]]$Samples) {
    if ($Samples.Count -lt 2) { return [double]0 }
    [double]$maximum = 0
    for ($index = 1; $index -lt $Samples.Count; ++$index) {
        $previous = [datetimeoffset]$Samples[$index - 1].timestamp
        $current = [datetimeoffset]$Samples[$index].timestamp
        $gap = ($current - $previous).TotalSeconds
        if ($gap -gt $maximum) { $maximum = $gap }
    }
    return $maximum
}

$resolvedExecutable = Get-CanonicalPath $Executable 'Executable'
$resolvedDatabase = Get-CanonicalPath $DatabasePath 'Database'
Assert-FixedLocalPath $resolvedDatabase 'DatabasePath'

$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
Assert-FixedLocalPath $resolvedOutput 'OutputDirectory'
[System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null
$samplesPath = Join-Path $resolvedOutput 'runtime-samples.jsonl'
$metadataPath = Join-Path $resolvedOutput 'runtime-metadata.json'
$summaryPath = Join-Path $resolvedOutput 'runtime-summary.json'
foreach ($evidencePath in @($samplesPath, $metadataPath, $summaryPath)) {
    if ([System.IO.File]::Exists($evidencePath)) {
        throw "Refusing to overwrite existing evidence: $evidencePath"
    }
}

$startedMonitoringAt = [datetimeoffset]::Now
if ($EndAt -le $startedMonitoringAt) {
    throw 'EndAt must be later than the monitor start time.'
}

$initial = Get-Process -Id $TargetProcessId -ErrorAction Stop
$initialExecutable = [System.IO.Path]::GetFullPath($initial.Path)
if (-not (Test-SamePath $initialExecutable $resolvedExecutable)) {
    throw "PID $TargetProcessId does not match the expected executable."
}
$expectedProcessStartTime = $initial.StartTime
$expectedProcessStartUtc = $initial.StartTime.ToUniversalTime()

$metadata = [PSCustomObject]@{
    processId = $TargetProcessId
    executable = $resolvedExecutable
    databasePath = $resolvedDatabase
    processStartedAt = $expectedProcessStartTime.ToString('o')
    monitoringStartedAt = $startedMonitoringAt.ToString('o')
    endAt = $EndAt.ToString('o')
    sampleSeconds = $effectiveSampleSeconds
    minimumRuntimeSeconds = $MinimumRuntimeSeconds
    workload = 'visible PPTrader local SQLite backtest long-run'
}
[System.IO.File]::WriteAllText(
    $metadataPath,
    ($metadata | ConvertTo-Json -Depth 4),
    $utf8NoBom)
[System.IO.File]::WriteAllText($samplesPath, '', $utf8NoBom)

$exitReason = 'end_time_reached'
$sampleCount = 0
while ([datetimeoffset]::Now -lt $EndAt) {
    $process = Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue
    if ($null -eq $process) {
        $exitReason = 'process_exited_early'
        break
    }

    try {
        $processExecutable = [System.IO.Path]::GetFullPath($process.Path)
        $processStartUtc = $process.StartTime.ToUniversalTime()
    } catch {
        $exitReason = 'process_identity_unavailable'
        break
    }
    if (-not (Test-SamePath $processExecutable $resolvedExecutable) -or
        $processStartUtc -ne $expectedProcessStartUtc) {
        $exitReason = 'process_identity_changed'
        break
    }

    $sampledAt = [datetimeoffset]::Now
    try {
        $databaseBytes = Get-FileSize $resolvedDatabase
        $walBytes = Get-FileSize ($resolvedDatabase + '-wal')
        $sample = [PSCustomObject]@{
            sampleIndex = $sampleCount
            timestamp = $sampledAt.ToString('o')
            privateBytes = [int64]$process.PrivateMemorySize64
            workingSetBytes = [int64]$process.WorkingSet64
            handleCount = [int]$process.HandleCount
            threadCount = [int]$process.Threads.Count
            cpuSeconds = [double]$process.CPU
            databaseBytes = $databaseBytes
            walBytes = $walBytes
            databaseAndWalBytes = [int64]($databaseBytes + $walBytes)
        }
    } catch {
        $exitReason = 'process_exited_during_sample'
        break
    }
    $line = ($sample | ConvertTo-Json -Compress) + [Environment]::NewLine
    [System.IO.File]::AppendAllText($samplesPath, $line, $utf8NoBom)
    ++$sampleCount

    $remainingSeconds = ($EndAt - [datetimeoffset]::Now).TotalSeconds
    if ($remainingSeconds -le 0) { break }
    $sleepSeconds = [Math]::Min($effectiveSampleSeconds, [Math]::Ceiling($remainingSeconds))
    Start-Sleep -Seconds $sleepSeconds
}

$finishedAt = [datetimeoffset]::Now
$samples = @(Get-Content -LiteralPath $samplesPath | Where-Object {
    -not [string]::IsNullOrWhiteSpace($_)
} | ForEach-Object { $_ | ConvertFrom-Json })
$privateValues = @($samples | ForEach-Object { [int64]$_.privateBytes } | Sort-Object)
$workingValues = @($samples | ForEach-Object { [int64]$_.workingSetBytes } | Sort-Object)
$handleValues = @($samples | ForEach-Object { [int]$_.handleCount } | Sort-Object)
$threadValues = @($samples | ForEach-Object { [int]$_.threadCount } | Sort-Object)
$databaseValues = @($samples | ForEach-Object { [int64]$_.databaseBytes } | Sort-Object)
$walValues = @($samples | ForEach-Object { [int64]$_.walBytes } | Sort-Object)

$matchingProcessStillRunning = $false
$finalProcess = Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue
if ($null -ne $finalProcess) {
    try {
        $finalExecutable = [System.IO.Path]::GetFullPath($finalProcess.Path)
        $finalStartUtc = $finalProcess.StartTime.ToUniversalTime()
        $matchingProcessStillRunning =
            (Test-SamePath $finalExecutable $resolvedExecutable) -and
            $finalStartUtc -eq $expectedProcessStartUtc
    } catch {
        $matchingProcessStillRunning = $false
    }
}

$actualRuntimeSeconds = [Math]::Max(
    0,
    ($finishedAt - $startedMonitoringAt).TotalSeconds)
$observedProcessExit = $exitReason -in @(
    'process_exited_early',
    'process_exited_during_sample')
if ($exitReason -eq 'process_identity_unavailable' -and $null -eq $finalProcess) {
    $observedProcessExit = $true
}
if ($observedProcessExit -and $actualRuntimeSeconds -ge $MinimumRuntimeSeconds) {
    $exitReason = 'process_exited_after_minimum_runtime'
}
$earlyExit = $exitReason -notin @(
    'end_time_reached',
    'process_exited_after_minimum_runtime')

$summary = [PSCustomObject]@{
    processId = $TargetProcessId
    processStartedAt = $expectedProcessStartTime.ToString('o')
    monitoringStartedAt = $startedMonitoringAt.ToString('o')
    finishedAt = $finishedAt.ToString('o')
    requestedEndAt = $EndAt.ToString('o')
    exitReason = $exitReason
    earlyExit = $earlyExit
    actualRuntimeSeconds = $actualRuntimeSeconds
    minimumRuntimeSeconds = $MinimumRuntimeSeconds
    processStillRunning = $matchingProcessStillRunning
    sampleCount = $samples.Count
    maximumSampleGapSeconds = Get-MaximumSampleGapSeconds $samples
    privateBytesP50 = Get-Percentile $privateValues 0.50
    privateBytesP95 = Get-Percentile $privateValues 0.95
    privateBytesMax = Get-Percentile $privateValues 1.00
    privateBytesSlopePerMinute = Get-SlopePerMinute $samples 'privateBytes'
    workingSetBytesP50 = Get-Percentile $workingValues 0.50
    workingSetBytesP95 = Get-Percentile $workingValues 0.95
    workingSetBytesMax = Get-Percentile $workingValues 1.00
    workingSetBytesSlopePerMinute = Get-SlopePerMinute $samples 'workingSetBytes'
    handleCountMax = if ($handleValues.Count) { [int]$handleValues[-1] } else { 0 }
    threadCountMax = if ($threadValues.Count) { [int]$threadValues[-1] } else { 0 }
    databaseBytesFirst = if ($samples.Count) { [int64]$samples[0].databaseBytes } else { 0 }
    databaseBytesLast = if ($samples.Count) { [int64]$samples[-1].databaseBytes } else { 0 }
    databaseBytesMax = Get-Percentile $databaseValues 1.00
    walBytesFirst = if ($samples.Count) { [int64]$samples[0].walBytes } else { 0 }
    walBytesLast = if ($samples.Count) { [int64]$samples[-1].walBytes } else { 0 }
    walBytesMax = Get-Percentile $walValues 1.00
}
[System.IO.File]::WriteAllText(
    $summaryPath,
    ($summary | ConvertTo-Json -Depth 4),
    $utf8NoBom)
