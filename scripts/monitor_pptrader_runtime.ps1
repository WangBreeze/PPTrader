param(
    [Parameter(Mandatory = $true)]
    [int]$TargetProcessId,
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory = $true)]
    [datetime]$EndAt,
    [int]$SampleSeconds = 30,
    [switch]$CloseAtEnd
)

$ErrorActionPreference = 'Stop'
$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null
$samplesPath = Join-Path $resolvedOutput 'runtime-samples.jsonl'
$metadataPath = Join-Path $resolvedOutput 'runtime-metadata.json'
$summaryPath = Join-Path $resolvedOutput 'runtime-summary.json'
$initial = Get-Process -Id $TargetProcessId -ErrorAction Stop
$initialExecutable = [System.IO.Path]::GetFullPath($initial.Path)
if (-not [string]::Equals(
        $initialExecutable.TrimEnd('\'),
        $resolvedExecutable.TrimEnd('\'),
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "PID $TargetProcessId does not match the expected PPTrader executable."
}
$startedAt = $initial.StartTime

[PSCustomObject]@{
    processId = $TargetProcessId
    executable = $resolvedExecutable
    startedAt = $startedAt.ToString('o')
    endAt = $EndAt.ToString('o')
    sampleSeconds = [Math]::Max(5, $SampleSeconds)
    sourceMode = 'real-time display, memory-only'
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $metadataPath -Encoding utf8

$exitReason = 'end_time_reached'
$sampleCount = 0
while ((Get-Date) -lt $EndAt) {
    $process = Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue
    $processExecutable = if ($null -ne $process) {
        [System.IO.Path]::GetFullPath($process.Path)
    } else { '' }
    if ($null -eq $process -or -not [string]::Equals(
            $processExecutable.TrimEnd('\'),
            $resolvedExecutable.TrimEnd('\'),
            [System.StringComparison]::OrdinalIgnoreCase)) {
        $exitReason = 'process_exited_early'
        break
    }
    # 只统计目标 PPTrader 的实际后代，避免把开发机上无关 Python 进程
    # 误算为行情桥。虚拟环境启动器与其真实解释器属于同一 bridge 组。
    $allProcesses = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
    $descendants = [System.Collections.Generic.List[object]]::new()
    $pendingParents = [System.Collections.Generic.Queue[int]]::new()
    $pendingParents.Enqueue($TargetProcessId)
    $seenProcessIds = [System.Collections.Generic.HashSet[int]]::new()
    [void]$seenProcessIds.Add($TargetProcessId)
    while ($pendingParents.Count -gt 0) {
        $parentId = $pendingParents.Dequeue()
        foreach ($child in @($allProcesses | Where-Object {
            [int]$_.ParentProcessId -eq $parentId
        })) {
            $childId = [int]$child.ProcessId
            if (-not $seenProcessIds.Add($childId)) { continue }
            $descendants.Add($child)
            $pendingParents.Enqueue($childId)
        }
    }
    $pythonRecords = @($descendants | Where-Object {
        $_.Name -match '^(python|pythonw)\.exe$'
    })
    $python = @($pythonRecords | ForEach-Object {
        Get-Process -Id ([int]$_.ProcessId) -ErrorAction SilentlyContinue
    } | Where-Object { $null -ne $_ })
    $bridgeRecords = @($descendants | Where-Object {
        $_.CommandLine -match '(?i)(^|[\\/])mootdx_bridge\.py([\s\"'']|$)'
    })
    $bridgeIds = [System.Collections.Generic.HashSet[int]]::new()
    foreach ($bridge in $bridgeRecords) {
        [void]$bridgeIds.Add([int]$bridge.ProcessId)
    }
    $bridgeRoots = @($bridgeRecords | Where-Object {
        -not $bridgeIds.Contains([int]$_.ParentProcessId)
    })
    [PSCustomObject]@{
        timestamp = (Get-Date).ToString('o')
        workingSetBytes = [int64]$process.WorkingSet64
        privateBytes = [int64]$process.PrivateMemorySize64
        handleCount = $process.HandleCount
        threadCount = $process.Threads.Count
        cpuSeconds = [double]$process.CPU
        pythonProcessCount = $python.Count
        pythonPrivateBytes = [int64](($python | Measure-Object -Property PrivateMemorySize64 -Sum).Sum)
        tdxBridgeProcessCount = $bridgeRecords.Count
        tdxBridgeGroupCount = $bridgeRoots.Count
        tdxBridgeProcessIds = @($bridgeRecords | ForEach-Object { [int]$_.ProcessId })
        tdxBridgeRootProcessIds = @($bridgeRoots | ForEach-Object { [int]$_.ProcessId })
    } | ConvertTo-Json -Compress | Add-Content -LiteralPath $samplesPath -Encoding utf8
    ++$sampleCount
    Start-Sleep -Seconds ([Math]::Max(5, $SampleSeconds))
}

$closeRequested = $false
$closedWithinTimeout = $false
if ($CloseAtEnd -and $exitReason -eq 'end_time_reached') {
    $process = Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue
    $processExecutable = if ($null -ne $process) {
        [System.IO.Path]::GetFullPath($process.Path)
    } else { '' }
    if ($null -ne $process -and [string]::Equals(
            $processExecutable.TrimEnd('\'),
            $resolvedExecutable.TrimEnd('\'),
            [System.StringComparison]::OrdinalIgnoreCase)) {
        $closeRequested = $process.CloseMainWindow()
        if ($closeRequested) {
            $closedWithinTimeout = $process.WaitForExit(20000)
        }
    } else {
        $closedWithinTimeout = $true
    }
}

$samples = if (Test-Path -LiteralPath $samplesPath) {
    @(Get-Content -LiteralPath $samplesPath | ForEach-Object { $_ | ConvertFrom-Json })
} else {
    @()
}
$privateValues = @($samples | ForEach-Object { [int64]$_.privateBytes } | Sort-Object)
$workingValues = @($samples | ForEach-Object { [int64]$_.workingSetBytes } | Sort-Object)
$bridgeProcessValues = @($samples | ForEach-Object {
    [int]$_.tdxBridgeProcessCount
} | Sort-Object)
$bridgeGroupValues = @($samples | ForEach-Object {
    [int]$_.tdxBridgeGroupCount
} | Sort-Object)
function Get-Percentile([object[]]$Values, [double]$Quantile) {
    if ($Values.Count -eq 0) { return 0 }
    $index = [Math]::Min($Values.Count - 1,
        [Math]::Max(0, [Math]::Ceiling($Values.Count * $Quantile) - 1))
    return [int64]$Values[$index]
}

function Get-SlopeBytesPerMinute([object[]]$Samples, [string]$Property) {
    if ($Samples.Count -lt 2) { return 0 }
    $origin = [datetime]$Samples[0].timestamp
    $points = @($Samples | ForEach-Object {
        [PSCustomObject]@{
            X = ([datetime]$_.timestamp - $origin).TotalSeconds
            Y = [double]($_.$Property)
        }
    })
    $meanX = ($points | Measure-Object -Property X -Average).Average
    $meanY = ($points | Measure-Object -Property Y -Average).Average
    [double]$numerator = 0
    [double]$denominator = 0
    foreach ($point in $points) {
        $dx = $point.X - $meanX
        $numerator += $dx * ($point.Y - $meanY)
        $denominator += $dx * $dx
    }
    if ($denominator -le 0) { return 0 }
    return [double](($numerator / $denominator) * 60.0)
}

$stillRunning = Get-Process -Id $TargetProcessId -ErrorAction SilentlyContinue
[PSCustomObject]@{
    processId = $TargetProcessId
    startedAt = $startedAt.ToString('o')
    finishedAt = (Get-Date).ToString('o')
    exitReason = $exitReason
    sampleCount = $sampleCount
    privateBytesFirst = if ($privateValues.Count) { [int64]$samples[0].privateBytes } else { 0 }
    privateBytesLast = if ($privateValues.Count) { [int64]$samples[-1].privateBytes } else { 0 }
    privateBytesDelta = if ($privateValues.Count) {
        [int64]$samples[-1].privateBytes - [int64]$samples[0].privateBytes
    } else { 0 }
    privateBytesSlopePerMinute = Get-SlopeBytesPerMinute $samples 'privateBytes'
    privateBytesP50 = Get-Percentile $privateValues 0.50
    privateBytesP95 = Get-Percentile $privateValues 0.95
    privateBytesMax = Get-Percentile $privateValues 1.00
    workingSetBytesMax = Get-Percentile $workingValues 1.00
    workingSetBytesSlopePerMinute = Get-SlopeBytesPerMinute $samples 'workingSetBytes'
    tdxBridgeProcessCountMax = if ($bridgeProcessValues.Count) {
        [int]$bridgeProcessValues[-1]
    } else { 0 }
    tdxBridgeGroupCountMax = if ($bridgeGroupValues.Count) {
        [int]$bridgeGroupValues[-1]
    } else { 0 }
    tdxBridgeMultipleGroupSampleCount = @($samples | Where-Object {
        [int]$_.tdxBridgeGroupCount -gt 1
    }).Count
    closeRequested = $closeRequested
    closedWithin20Seconds = $closedWithinTimeout
    processStillRunning = $null -ne $stillRunning
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding utf8
