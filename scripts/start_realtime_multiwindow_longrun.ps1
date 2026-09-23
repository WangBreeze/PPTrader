param(
    [string]$EvidenceDirectory =
        'I:\workspace\PPTrader\artifacts\realtime-multiwindow-longrun-2026-09-02',
    [datetime]$StartAt = [datetime]'2026-09-02 09:25:00',
    [datetime]$EndAt = [datetime]'2026-09-02 15:31:00',
    [string]$StrategyStudyPlanPath = '',
    [string]$AcceptanceUniverseSymbols = '',
    [int]$MaximumProviderBatches = 0,
    [string]$ApplicationExecutable = '',
    [string]$QtRuntimeRoot = '',
    [string]$SchedulerTaskName = '',
    [string]$ExpectedApplicationSha256 = '',
    [ValidateRange(1, 10)]
    [int]$MaximumDirectLaunchLeadMinutes = 10,
    [switch]$Worker
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..'))
$artifactRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'artifacts'))
$resolvedEvidence = [System.IO.Path]::GetFullPath($EvidenceDirectory)
$resolvedStrategyPlan = if ([string]::IsNullOrWhiteSpace($StrategyStudyPlanPath)) {
    ''
} else {
    [System.IO.Path]::GetFullPath($StrategyStudyPlanPath)
}
if (-not $resolvedEvidence.StartsWith(
        $artifactRoot.TrimEnd('\') + '\',
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Evidence directory must be inside the repository artifacts directory.'
}
if ($EndAt -le $StartAt) {
    throw 'EndAt must be later than StartAt.'
}
if ($resolvedStrategyPlan -and -not (Test-Path -LiteralPath $resolvedStrategyPlan)) {
    throw "Strategy study plan does not exist: $resolvedStrategyPlan"
}

$executable = if ([string]::IsNullOrWhiteSpace($ApplicationExecutable)) {
    [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot `
        'out\build\vs2022-x64\Release\PPTrader.exe'))
} else {
    [System.IO.Path]::GetFullPath($ApplicationExecutable)
}
$resolvedQtRoot = if ([string]::IsNullOrWhiteSpace($QtRuntimeRoot)) {
    ''
} else {
    [System.IO.Path]::GetFullPath($QtRuntimeRoot)
}
$expectedQtCore = if ($resolvedQtRoot) {
    Join-Path $resolvedQtRoot 'bin\Qt6Core.dll'
} else {
    ''
}
$expectedPlatformPlugin = if ($resolvedQtRoot) {
    Join-Path $resolvedQtRoot 'plugins\platforms\qwindows.dll'
} else {
    ''
}
$python = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot '.venv\Scripts\python.exe'))
$monitorScript = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'scripts\monitor_pptrader_runtime.ps1'))
$powershell = Join-Path $env:SystemRoot `
    'System32\WindowsPowerShell\v1.0\powershell.exe'
$requiredFiles = @($executable, $python, $monitorScript, $powershell)
if ($resolvedQtRoot) {
    $requiredFiles += @($expectedQtCore, $expectedPlatformPlugin)
}
foreach ($required in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing acceptance dependency: $required"
    }
}
if ($ExpectedApplicationSha256) {
    if ($ExpectedApplicationSha256 -notmatch '^[A-Fa-f0-9]{64}$') {
        throw 'Expected application hash must be 64 hexadecimal characters.'
    }
    $actualApplicationSha256 = (Get-FileHash -LiteralPath $executable `
        -Algorithm SHA256).Hash
    if (-not $actualApplicationSha256.Equals(
            $ExpectedApplicationSha256,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'PPTrader executable changed after acceptance registration.'
    }
}

[System.IO.Directory]::CreateDirectory($resolvedEvidence) | Out-Null
$launchMetadataPath = Join-Path $resolvedEvidence 'launch-metadata.json'

if (-not $Worker) {
    & $PSCommandPath -EvidenceDirectory $resolvedEvidence `
        -StartAt $StartAt -EndAt $EndAt `
        -StrategyStudyPlanPath $resolvedStrategyPlan `
        -AcceptanceUniverseSymbols $AcceptanceUniverseSymbols `
        -MaximumProviderBatches $MaximumProviderBatches `
        -ApplicationExecutable $executable `
        -QtRuntimeRoot $resolvedQtRoot `
        -SchedulerTaskName $SchedulerTaskName `
        -ExpectedApplicationSha256 $ExpectedApplicationSha256 `
        -MaximumDirectLaunchLeadMinutes $MaximumDirectLaunchLeadMinutes `
        -Worker
    exit $LASTEXITCODE
}

$earliestDirectStart = $StartAt.AddMinutes(-$MaximumDirectLaunchLeadMinutes)
if ((Get-Date) -lt $earliestDirectStart) {
    throw ('Direct launch is too early. Use a durable scheduled task; ' +
        "this worker may start no earlier than $($earliestDirectStart.ToString('o')).")
}
while ((Get-Date) -lt $StartAt) {
    $remainingSeconds = [Math]::Ceiling(($StartAt - (Get-Date)).TotalSeconds)
    Start-Sleep -Seconds ([Math]::Max(1, [Math]::Min(30, $remainingSeconds)))
}
if ((Get-Date) -ge $EndAt.AddSeconds(-30)) {
    throw 'The formal acceptance window has already ended.'
}
if (Test-Path -LiteralPath $launchMetadataPath) {
    throw 'Launch metadata already exists; duplicate launch rejected.'
}

$allProcesses = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
$duplicateApplications = @($allProcesses | Where-Object {
    $_.Name -ieq 'PPTrader.exe' -or
    $_.CommandLine -match '(?i)--realtime-multiwindow-acceptance'
})
if ($duplicateApplications.Count -gt 0) {
    throw 'An active PPTrader or dual-window acceptance process already exists.'
}
$activeBridges = @($allProcesses | Where-Object {
    $_.CommandLine -match '(?i)(^|[\\/])mootdx_bridge\.py([\s"'']|$)'
})
if ($activeBridges.Count -gt 0) {
    throw 'An active TDX bridge already exists; duplicate bridge launch rejected.'
}

$actualStart = Get-Date
$durationSeconds = [Math]::Floor(($EndAt - $actualStart).TotalSeconds)
if ($durationSeconds -lt 30 -or $durationSeconds -gt 23000) {
    throw "Invalid acceptance duration: $durationSeconds seconds."
}

$env:PPTRADER_PYTHON = $python
$env:PPTRADER_REALTIME_MULTIWINDOW_OUTPUT = $resolvedEvidence
$env:PPTRADER_REALTIME_MULTIWINDOW_SECONDS = [string]$durationSeconds
$env:PPTRADER_REALTIME_MULTIWINDOW_SAMPLE_SECONDS = '15'
$normalizedAcceptanceSymbols = (($AcceptanceUniverseSymbols -split ',') |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -match '^\d{6}$' } |
    Select-Object -Unique) -join ','
if ($normalizedAcceptanceSymbols) {
    $env:PPTRADER_REALTIME_ACCEPTANCE_UNIVERSE_SYMBOLS =
        $normalizedAcceptanceSymbols
} else {
    Remove-Item Env:PPTRADER_REALTIME_ACCEPTANCE_UNIVERSE_SYMBOLS `
        -ErrorAction SilentlyContinue
}
if ($MaximumProviderBatches -gt 0) {
    $env:PPTRADER_TDX_MAX_BATCHES = [string]$MaximumProviderBatches
    $env:PPTRADER_TENCENT_MAX_BATCHES = [string]$MaximumProviderBatches
} else {
    Remove-Item Env:PPTRADER_TDX_MAX_BATCHES -ErrorAction SilentlyContinue
    Remove-Item Env:PPTRADER_TENCENT_MAX_BATCHES -ErrorAction SilentlyContinue
}
if ($resolvedStrategyPlan) {
    $studyPlan = Get-Content -Raw -Encoding UTF8 -LiteralPath $resolvedStrategyPlan |
        ConvertFrom-Json
    $thirdBoard = @($studyPlan.groups.thirdBoard)
    $secondBoard = @($studyPlan.groups.secondBoard)
    if ($thirdBoard.Count -eq 0 -or $secondBoard.Count -eq 0) {
        throw 'Strategy study plan must contain secondBoard and thirdBoard candidates.'
    }
    $env:PPTRADER_REALTIME_WINDOW_A_SYMBOLS =
        (($thirdBoard | Select-Object -First 4) -join ',')
    $env:PPTRADER_REALTIME_WINDOW_B_SYMBOLS =
        (($secondBoard | Select-Object -First 4) -join ',')
    $env:PPTRADER_LIMIT_UP_STRATEGY_PLAN = $resolvedStrategyPlan
} else {
    $env:PPTRADER_REALTIME_WINDOW_A_SYMBOLS = '600519,000333,601318'
    $env:PPTRADER_REALTIME_WINDOW_B_SYMBOLS = '000001,000858,600036'
    Remove-Item Env:PPTRADER_LIMIT_UP_STRATEGY_PLAN -ErrorAction SilentlyContinue
}
$env:PPTRADER_DATABASE_PATH = Join-Path $resolvedEvidence 'acceptance.db'
$env:PPTRADER_LOG_PATH = Join-Path $resolvedEvidence 'pptrader.log'
$env:PPTRADER_ACCEPTANCE_DISABLE_ACCESSIBILITY = '1'
Remove-Item Env:PPTRADER_NAS_TOKEN -ErrorAction SilentlyContinue
Remove-Item Env:PPTRADER_NAS_TOKEN_FILE -ErrorAction SilentlyContinue
if ($resolvedQtRoot) {
    $qtBin = Join-Path $resolvedQtRoot 'bin'
    $env:PATH = $qtBin + ';' + $env:PATH
    $env:QT_PLUGIN_PATH = Join-Path $resolvedQtRoot 'plugins'
    $env:QML2_IMPORT_PATH = Join-Path $resolvedQtRoot 'qml'
}

$application = Start-Process -FilePath $executable `
    -ArgumentList '--realtime-multiwindow-acceptance' `
    -WorkingDirectory $repositoryRoot -PassThru `
    -RedirectStandardOutput (Join-Path $resolvedEvidence 'runtime.stdout.log') `
    -RedirectStandardError (Join-Path $resolvedEvidence 'runtime.stderr.log')

$loadedQtCorePath = ''
$loadedQtCoreVersion = ''
for ($attempt = 0; $attempt -lt 50 -and -not $application.HasExited; $attempt++) {
    try {
        $application.Refresh()
        $qtModule = $application.Modules |
            Where-Object { $_.ModuleName -ieq 'Qt6Core.dll' } |
            Select-Object -First 1
        if ($qtModule) {
            $loadedQtCorePath = [System.IO.Path]::GetFullPath($qtModule.FileName)
            $loadedQtCoreVersion = $qtModule.FileVersionInfo.FileVersion
            break
        }
    } catch {
        # The module list can be temporarily unavailable while the GUI starts.
    }
    Start-Sleep -Milliseconds 100
}
if ($resolvedQtRoot -and (
        -not $loadedQtCorePath -or
        -not $loadedQtCorePath.Equals(
            [System.IO.Path]::GetFullPath($expectedQtCore),
            [System.StringComparison]::OrdinalIgnoreCase))) {
    if (-not $application.HasExited) {
        $application.CloseMainWindow() | Out-Null
        if (-not $application.WaitForExit(5000)) {
            Stop-Process -Id $application.Id -Force -ErrorAction SilentlyContinue
        }
    }
    throw "PPTrader loaded an unexpected Qt runtime: $loadedQtCorePath"
}

$monitorEnd = $EndAt.AddSeconds(-5)
$monitorArguments = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', $monitorScript,
    '-TargetProcessId', [string]$application.Id,
    '-Executable', $executable,
    '-OutputDirectory', $resolvedEvidence,
    '-EndAt', $monitorEnd.ToString('o'),
    '-SampleSeconds', '30'
)
$monitor = Start-Process -FilePath $powershell `
    -ArgumentList $monitorArguments -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $resolvedEvidence 'monitor.stdout.log') `
    -RedirectStandardError (Join-Path $resolvedEvidence 'monitor.stderr.log')

[PSCustomObject]@{
    applicationPid = $application.Id
    monitorPid = $monitor.Id
    launcherPid = $PID
    schedulerTaskName = $SchedulerTaskName
    executable = $executable
    executableSha256 = (Get-FileHash -LiteralPath $executable `
        -Algorithm SHA256).Hash
    expectedApplicationSha256 = $ExpectedApplicationSha256
    qtRuntimeRoot = $resolvedQtRoot
    expectedQtCorePath = $expectedQtCore
    loadedQtCorePath = $loadedQtCorePath
    loadedQtCoreVersion = $loadedQtCoreVersion
    evidenceDirectory = $resolvedEvidence
    scheduledStartAt = $StartAt.ToString('o')
    actualStartedAt = $actualStart.ToString('o')
    endAt = $EndAt.ToString('o')
    durationSeconds = $durationSeconds
    sampleSeconds = 15
    runtimeSampleSeconds = 30
    databasePath = $env:PPTRADER_DATABASE_PATH
    sourceMode = 'real-time display, memory-only'
    strategyStudyPlanPath = $resolvedStrategyPlan
    acceptanceUniverseSymbols = $normalizedAcceptanceSymbols
    maximumProviderBatches = $MaximumProviderBatches
    networkPermission = if ($SchedulerTaskName) {
        'interactive Windows Task Scheduler context'
    } else {
        'inherited from the approved launcher process'
    }
} | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $launchMetadataPath -Encoding utf8

Write-Output "Dual-window acceptance started: app PID $($application.Id), monitor PID $($monitor.Id)."

$application.WaitForExit()
$application.Refresh()
$applicationExitCode = $null
try {
    if ($application.HasExited) {
        $rawExitCode = $application.ExitCode
        if ($null -ne $rawExitCode) {
            $parsedExitCode = 0
            if ([int]::TryParse([string]$rawExitCode, [ref]$parsedExitCode)) {
                $applicationExitCode = $parsedExitCode
            }
        }
    }
} catch {
    # 进程句柄在退出瞬间可能尚未刷新；下面用 PID 独立确认回收。
}
$applicationProcessGone = $false
for ($poll = 0; $poll -lt 75 -and -not $applicationProcessGone; $poll++) {
    try {
        $applicationProcessGone = $null -eq (Get-Process -Id $application.Id -ErrorAction SilentlyContinue)
    } catch {
        $applicationProcessGone = $true
    }
    if (-not $applicationProcessGone) { Start-Sleep -Milliseconds 200 }
}
$monitor.WaitForExit()
$runtimeSamplesPath = Join-Path $resolvedEvidence 'runtime-samples.jsonl'
$observedBridgeProcessIds = [System.Collections.Generic.HashSet[int]]::new()
if (Test-Path -LiteralPath $runtimeSamplesPath) {
    foreach ($sampleLine in Get-Content -LiteralPath $runtimeSamplesPath) {
        if ([string]::IsNullOrWhiteSpace($sampleLine)) { continue }
        $sample = $sampleLine | ConvertFrom-Json
        foreach ($bridgeProcessId in @($sample.tdxBridgeProcessIds)) {
            [void]$observedBridgeProcessIds.Add([int]$bridgeProcessId)
        }
    }
}
$bridgeDrainDeadline = (Get-Date).AddSeconds(20)
$bridgeProcessIdsStillRunning = @()
do {
    $bridgeProcessIdsStillRunning = @($observedBridgeProcessIds | Where-Object {
        $null -ne (Get-Process -Id $_ -ErrorAction SilentlyContinue)
    })
    if ($bridgeProcessIdsStillRunning.Count -eq 0) { break }
    Start-Sleep -Milliseconds 200
} while ((Get-Date) -lt $bridgeDrainDeadline)
$bridgeDrainCompleted = $bridgeProcessIdsStillRunning.Count -eq 0
[PSCustomObject]@{
    applicationPid = $application.Id
    monitorPid = $monitor.Id
    finishedAt = (Get-Date).ToString('o')
    applicationExitCode = $applicationExitCode
    applicationProcessGone = $applicationProcessGone
    monitorExitCode = $monitor.ExitCode
    tdxBridgeProcessIdsObserved = @($observedBridgeProcessIds)
    tdxBridgeProcessIdsStillRunning = $bridgeProcessIdsStillRunning
    tdxBridgeDrainCompleted = $bridgeDrainCompleted
} | ConvertTo-Json -Depth 3 |
    Set-Content -LiteralPath (Join-Path $resolvedEvidence 'launcher-exit.json') `
        -Encoding utf8
if (($null -ne $applicationExitCode) -and ($applicationExitCode -ne 0)) {
    throw "PPTrader exited with code $applicationExitCode."
}
if (-not $applicationProcessGone) {
    throw "PPTrader process $($application.Id) did not exit after its summary completed."
}
if (-not $bridgeDrainCompleted) {
    throw "TDX bridge descendants did not drain after PPTrader exited: $($bridgeProcessIdsStillRunning -join ',')."
}
