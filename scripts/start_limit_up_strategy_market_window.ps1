param(
    [Parameter(Mandatory = $true)]
    [datetime]$TradingDay,
    [Parameter(Mandatory = $true)]
    [string]$StrategyStudyPlanPath,
    [Parameter(Mandatory = $true)]
    [string]$EvidenceRoot,
    [Parameter(Mandatory = $true)]
    [string]$ApplicationExecutable,
    [Parameter(Mandatory = $true)]
    [string]$QtRuntimeRoot,
    [string]$SchedulerTaskName = '',
    [Parameter(Mandatory = $true)]
    [string]$ExpectedStrategyPlanSha256,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedOrchestratorSha256,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedRunnerSha256,
    [Parameter(Mandatory = $true)]
    [string]$ExpectedApplicationSha256,
    [ValidateRange(1, 10)]
    [int]$MaximumDirectLaunchLeadMinutes = 10
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'artifacts'))
$resolvedRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$resolvedPlan = [System.IO.Path]::GetFullPath($StrategyStudyPlanPath)
$resolvedExecutable = if ([string]::IsNullOrWhiteSpace($ApplicationExecutable)) {
    ''
} else {
    [System.IO.Path]::GetFullPath($ApplicationExecutable)
}
$resolvedQtRoot = if ([string]::IsNullOrWhiteSpace($QtRuntimeRoot)) {
    ''
} else {
    [System.IO.Path]::GetFullPath($QtRuntimeRoot)
}
$runner = Join-Path $PSScriptRoot 'start_realtime_multiwindow_longrun.ps1'
if (-not $resolvedRoot.StartsWith(
        $artifactRoot.TrimEnd('\') + '\',
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Evidence root must be inside the repository artifacts directory.'
}
foreach ($required in @($resolvedPlan, $runner)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing strategy acceptance dependency: $required"
    }
}
foreach ($expectedHash in @(
        $ExpectedStrategyPlanSha256, $ExpectedOrchestratorSha256,
        $ExpectedRunnerSha256, $ExpectedApplicationSha256)) {
    if ($expectedHash -notmatch '^[A-Fa-f0-9]{64}$') {
        throw 'Expected content hashes must be 64 hexadecimal characters.'
    }
}
$actualHashes = [ordered]@{
    strategyStudyPlan = (Get-FileHash -LiteralPath $resolvedPlan `
        -Algorithm SHA256).Hash
    orchestrator = (Get-FileHash -LiteralPath $PSCommandPath `
        -Algorithm SHA256).Hash
    runner = (Get-FileHash -LiteralPath $runner -Algorithm SHA256).Hash
    application = (Get-FileHash -LiteralPath $resolvedExecutable `
        -Algorithm SHA256).Hash
}
$expectedHashes = [ordered]@{
    strategyStudyPlan = $ExpectedStrategyPlanSha256
    orchestrator = $ExpectedOrchestratorSha256
    runner = $ExpectedRunnerSha256
    application = $ExpectedApplicationSha256
}
foreach ($key in $expectedHashes.Keys) {
    if (-not $actualHashes[$key].Equals(
            $expectedHashes[$key],
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Frozen acceptance content changed after registration: $key"
    }
}

$plan = Get-Content -Raw -Encoding UTF8 -LiteralPath $resolvedPlan |
    ConvertFrom-Json
$targetDay = [datetime]::ParseExact(
    [string]$plan.targetTradingDay, 'yyyy-MM-dd',
    [System.Globalization.CultureInfo]::InvariantCulture)
if ($targetDay.Date -ne $TradingDay.Date) {
    throw "Plan target day $($targetDay.ToString('yyyy-MM-dd')) does not match requested trading day."
}
$probeSymbol = [string](@($plan.groups.thirdBoard)[0])
if ($probeSymbol -notmatch '^\d{6}$') {
    throw 'The frozen plan does not contain a valid third-board probe symbol.'
}

[System.IO.Directory]::CreateDirectory($resolvedRoot) | Out-Null
$statusPath = Join-Path $resolvedRoot 'status.json'
$preflightDirectory = Join-Path $resolvedRoot 'preflight-one-symbol-one-batch'
$formalDirectory = Join-Path $resolvedRoot 'formal'
if (Test-Path -LiteralPath $statusPath) {
    throw 'Strategy market-window status already exists; duplicate orchestrator rejected.'
}

function Write-Status([string]$phase, [string]$message, [hashtable]$extra = @{}) {
    $payload = [ordered]@{
        phase = $phase
        message = $message
        updatedAt = (Get-Date).ToString('o')
        orchestratorPid = $PID
        planVersion = [string]$plan.planVersion
        targetTradingDay = $TradingDay.ToString('yyyy-MM-dd')
        probeSymbol = $probeSymbol
        preflightDirectory = $preflightDirectory
        formalDirectory = $formalDirectory
        schedulerTaskName = $SchedulerTaskName
        applicationExecutable = $resolvedExecutable
        qtRuntimeRoot = $resolvedQtRoot
        maximumDirectLaunchLeadMinutes = $MaximumDirectLaunchLeadMinutes
        expectedContentHashes = $expectedHashes
        actualContentHashes = $actualHashes
    }
    foreach ($key in $extra.Keys) { $payload[$key] = $extra[$key] }
    $payload | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $statusPath -Encoding utf8
}

function Write-Lease([string]$phase, [datetime]$deadline) {
    [ordered]@{
        orchestratorPid = $PID
        phase = $phase
        observedAt = (Get-Date).ToString('o')
        deadline = $deadline.ToString('o')
        schedulerTaskName = $SchedulerTaskName
    } | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $resolvedRoot `
            'orchestrator-lease.json') -Encoding utf8
}

function Wait-Until([datetime]$deadline, [string]$phase) {
    while ((Get-Date) -lt $deadline) {
        Write-Lease $phase $deadline
        $remaining = [Math]::Ceiling(($deadline - (Get-Date)).TotalSeconds)
        Start-Sleep -Seconds ([Math]::Max(1, [Math]::Min(30, $remaining)))
    }
    Write-Lease $phase $deadline
}

function Assert-Preflight {
    $summaryPath = Join-Path $preflightDirectory 'summary.json'
    $launcherExitPath = Join-Path $preflightDirectory 'launcher-exit.json'
    $framesPath = Join-Path $preflightDirectory 'limit-up-strategy-frames.jsonl'
    $runtimeErrorPath = Join-Path $preflightDirectory 'runtime.stderr.log'
    $applicationLogPath = Join-Path $preflightDirectory 'pptrader.log'
    foreach ($required in @($summaryPath, $launcherExitPath, $framesPath,
                             $runtimeErrorPath, $applicationLogPath)) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "Preflight output is missing: $required"
        }
    }
    $summary = Get-Content -Raw -Encoding UTF8 -LiteralPath $summaryPath |
        ConvertFrom-Json
    $launcherExit = Get-Content -Raw -Encoding UTF8 -LiteralPath $launcherExitPath |
        ConvertFrom-Json
    $frames = @(Get-Content -Encoding UTF8 -LiteralPath $framesPath |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { $_ | ConvertFrom-Json })
    $permissionErrors = Select-String -Path $applicationLogPath,$runtimeErrorPath `
        -Pattern 'Permission denied|SocketAccessError|Network access is disabled' `
        -SimpleMatch:$false -ErrorAction SilentlyContinue
    $completeFrames = @($frames | Where-Object {
        [int]$_.snapshotExpectedCount -eq 1 -and
        [int]$_.snapshotReceivedCount -eq 1 -and
        [double]$_.snapshotCompleteness -ge 1.0
    })
    if (-not [bool]$summary.completed -or [string]$summary.error) {
        throw "Preflight application summary failed: $($summary.error)"
    }
    if (-not [bool]$launcherExit.applicationProcessGone -or
        [int]$launcherExit.applicationExitCode -ne 0 -or
        [int]$launcherExit.monitorExitCode -ne 0) {
        throw 'Preflight process did not exit cleanly.'
    }
    if ((Get-Item -LiteralPath $runtimeErrorPath).Length -gt 0) {
        throw 'Preflight runtime stderr is not empty.'
    }
    if ($permissionErrors) {
        throw 'Preflight encountered a network-permission error.'
    }
    if ($completeFrames.Count -lt 1) {
        throw 'Preflight did not receive a complete 1/1 real-time snapshot.'
    }
    return $completeFrames[-1]
}

$preflightStart = $TradingDay.Date.AddHours(9).AddMinutes(25)
$preflightEnd = $TradingDay.Date.AddHours(9).AddMinutes(28)
$formalStart = $TradingDay.Date.AddHours(9).AddMinutes(30)
$formalEnd = $TradingDay.Date.AddHours(15).AddMinutes(31)
if ((Get-Date) -ge $preflightEnd) {
    throw 'The one-symbol preflight window has already ended.'
}
$earliestDirectStart = $preflightStart.AddMinutes(
    -$MaximumDirectLaunchLeadMinutes)
if ((Get-Date) -lt $earliestDirectStart) {
    throw ('Direct launch is too early. Register a durable scheduled task; ' +
        "the orchestrator may start no earlier than $($earliestDirectStart.ToString('o')).")
}

try {
    Write-Status 'waiting_preflight' '等待 09:25 同二进制、单代码、单批次真实网络门禁。'
    Wait-Until $preflightStart 'waiting_preflight'
    Write-Status 'preflight_running' '正在运行单代码、单批次真实网络门禁。'
    & $runner -EvidenceDirectory $preflightDirectory `
        -StartAt $preflightStart -EndAt $preflightEnd `
        -StrategyStudyPlanPath $resolvedPlan `
        -AcceptanceUniverseSymbols $probeSymbol `
        -MaximumProviderBatches 1 `
        -ApplicationExecutable $resolvedExecutable `
        -QtRuntimeRoot $resolvedQtRoot `
        -SchedulerTaskName $SchedulerTaskName `
        -ExpectedApplicationSha256 $ExpectedApplicationSha256
    $preflightFrame = Assert-Preflight

    Write-Status 'preflight_passed' '门禁通过；等待 09:30 全市场正式段。' @{
        preflightBatchId = [string]$preflightFrame.batchId
        preflightReceivedCount = [int]$preflightFrame.snapshotReceivedCount
    }
    Wait-Until $formalStart 'waiting_formal'
    Write-Status 'formal_running' '全市场涨停候选前瞻观察正式运行中。'
    & $runner -EvidenceDirectory $formalDirectory `
        -StartAt $formalStart -EndAt $formalEnd `
        -StrategyStudyPlanPath $resolvedPlan `
        -ApplicationExecutable $resolvedExecutable `
        -QtRuntimeRoot $resolvedQtRoot `
        -SchedulerTaskName $SchedulerTaskName `
        -ExpectedApplicationSha256 $ExpectedApplicationSha256
    Write-Status 'completed' '正式段已结束，等待汇总、复核和文档结论。'
} catch {
    Write-Status 'failed' $_.Exception.Message @{
        failureType = $_.Exception.GetType().FullName
    }
    $_ | Out-String | Set-Content -LiteralPath (
        Join-Path $resolvedRoot 'failure.txt') -Encoding utf8
    throw
}
