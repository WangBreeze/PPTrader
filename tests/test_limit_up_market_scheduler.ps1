param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$registrationScript = Join-Path $repositoryRoot 'scripts\register_limit_up_strategy_market_task.ps1'
$orchestratorScript = Join-Path $repositoryRoot 'scripts\start_limit_up_strategy_market_window.ps1'
$runnerScript = Join-Path $repositoryRoot 'scripts\start_realtime_multiwindow_longrun.ps1'
$qtExecutable = Join-Path $repositoryRoot 'out\build\vs2022-qt611\Release\PPTrader.exe'
$qtRoot = 'D:\ProgramData\Qt\6.11.2\msvc2022_64'

if (-not (Test-Path -LiteralPath $registrationScript)) {
    throw 'Durable scheduler registration script is missing.'
}

$tradingDay = (Get-Date).Date.AddDays(2)
while ($tradingDay.DayOfWeek -in @(
        [DayOfWeek]::Saturday, [DayOfWeek]::Sunday)) {
    $tradingDay = $tradingDay.AddDays(1)
}

$fixtureDirectory = Join-Path ([System.IO.Path]::GetTempPath()) (
    'pptrader-scheduler-test-' + [guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($fixtureDirectory) | Out-Null
try {
    $planPath = Join-Path $fixtureDirectory 'strategy-study-plan.json'
    [ordered]@{
        planVersion = 'scheduler-regression-v1'
        targetTradingDay = $tradingDay.ToString('yyyy-MM-dd')
        groups = [ordered]@{
            thirdBoard = @('000523')
            secondBoard = @('000019')
            reversalBoard = @()
        }
    } | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath $planPath -Encoding utf8

    $testId = [guid]::NewGuid().ToString('N')
    $evidenceRoot = Join-Path $repositoryRoot (
        'artifacts\scheduler-dry-run-' + $testId)
    $json = & $registrationScript -TradingDay $tradingDay `
        -StrategyStudyPlanPath $planPath -EvidenceRoot $evidenceRoot `
        -ApplicationExecutable $qtExecutable -QtRuntimeRoot $qtRoot `
        -TaskName ('PPTrader-LimitUp-' + $tradingDay.ToString('yyyyMMdd')) `
        -DryRun
    if ($LASTEXITCODE -ne 0) {
        throw "Dry-run scheduler failed with exit code $LASTEXITCODE."
    }
    $plan = $json | ConvertFrom-Json

    if (-not [bool]$plan.durableScheduler) {
        throw 'The plan does not identify a durable scheduler.'
    }
    if ([string]$plan.logonType -ne 'Interactive') {
        throw 'The visible PPTrader task must use an interactive logon.'
    }
    if ([int]$plan.leadSeconds -ne 60) {
        throw "Expected a 60-second launch lead, got $($plan.leadSeconds)."
    }
    if ([int]$plan.executionTimeLimitHours -lt 8) {
        throw 'The scheduled task limit must cover the complete trading window.'
    }
    if ([int]$plan.directLaunchMaximumLeadMinutes -gt 10) {
        throw 'Direct launch still permits an unsafe long-lived waiting process.'
    }
    if ([string]$plan.qtCoreVersion -ne '6.11.2.0') {
        throw "Expected Qt 6.11.2.0, got $($plan.qtCoreVersion)."
    }
    if ([System.IO.Path]::GetFullPath([string]$plan.applicationExecutable) -ne
        [System.IO.Path]::GetFullPath($qtExecutable)) {
        throw 'The task does not pin the requested Qt 6.11 PPTrader executable.'
    }
    foreach ($hashField in @(
            'strategyStudyPlanSha256', 'orchestratorSha256',
            'runnerSha256', 'applicationSha256')) {
        if ([string]$plan.$hashField -notmatch '^[A-Fa-f0-9]{64}$') {
            throw "Frozen content hash is missing: $hashField"
        }
    }

    $approvalRejected = $false
    try {
        & $registrationScript -TradingDay $tradingDay `
            -StrategyStudyPlanPath $planPath -EvidenceRoot $evidenceRoot `
            -ApplicationExecutable $qtExecutable -QtRuntimeRoot $qtRoot `
            -TaskName ('PPTrader-LimitUp-' + $tradingDay.ToString('yyyyMMdd'))
    } catch {
        $approvalRejected = $_.Exception.Message -match 'network approval reference'
    }
    if (-not $approvalRejected) {
        throw 'A real scheduled task could be registered without explicit network approval evidence.'
    }

    $source = Get-Content -LiteralPath $orchestratorScript -Raw
    foreach ($requiredMarker in @(
            'MaximumDirectLaunchLeadMinutes',
            'orchestrator-lease.json',
            'SchedulerTaskName')) {
        if (-not $source.Contains($requiredMarker)) {
            throw "Orchestrator safety marker is missing: $requiredMarker"
        }
    }
    if ($source.Contains("[datetime]`$TradingDay =") -or
        $source.Contains('limit-up-strategy-market-2026-09-10')) {
        throw 'The reusable orchestrator still contains a stale trading-day default.'
    }

    $registrationSource = Get-Content -LiteralPath $registrationScript -Raw
    foreach ($requiredMarker in @(
            'New-ScheduledTaskSettingsSet',
            '-MultipleInstances IgnoreNew',
            '-ExecutionTimeLimit')) {
        if (-not $registrationSource.Contains($requiredMarker)) {
            throw "Scheduled-task safety marker is missing: $requiredMarker"
        }
    }

    $directEvidenceRoot = Join-Path $repositoryRoot (
        'artifacts\scheduler-direct-launch-' + $testId)
    $planSha256 = (Get-FileHash -LiteralPath $planPath -Algorithm SHA256).Hash
    $orchestratorSha256 = (Get-FileHash -LiteralPath $orchestratorScript `
        -Algorithm SHA256).Hash
    $runnerSha256 = (Get-FileHash -LiteralPath $runnerScript `
        -Algorithm SHA256).Hash
    $applicationSha256 = (Get-FileHash -LiteralPath $qtExecutable `
        -Algorithm SHA256).Hash
    $directLaunchRejected = $false
    try {
        & $orchestratorScript -TradingDay $tradingDay `
            -StrategyStudyPlanPath $planPath `
            -EvidenceRoot $directEvidenceRoot `
            -ApplicationExecutable $qtExecutable `
            -QtRuntimeRoot $qtRoot `
            -ExpectedStrategyPlanSha256 $planSha256 `
            -ExpectedOrchestratorSha256 $orchestratorSha256 `
            -ExpectedRunnerSha256 $runnerSha256 `
            -ExpectedApplicationSha256 $applicationSha256
    } catch {
        $directLaunchRejected =
            $_.Exception.Message -match 'durable scheduled task'
    }
    if (-not $directLaunchRejected) {
        throw 'The orchestrator did not reject an unsafe early direct launch.'
    }

    $runnerEvidenceRoot = Join-Path $repositoryRoot (
        'artifacts\scheduler-runner-launch-' + $testId)
    $runnerLaunchRejected = $false
    try {
        & $runnerScript -EvidenceDirectory $runnerEvidenceRoot `
            -StartAt $tradingDay.Date.AddHours(9).AddMinutes(25) `
            -EndAt $tradingDay.Date.AddHours(9).AddMinutes(28) `
            -ApplicationExecutable $qtExecutable `
            -QtRuntimeRoot $qtRoot `
            -ExpectedApplicationSha256 $applicationSha256 -Worker
    } catch {
        $runnerLaunchRejected =
            $_.Exception.Message -match 'durable scheduled task'
    }
    if (-not $runnerLaunchRejected) {
        throw 'The application runner did not reject an unsafe early launch.'
    }

    Write-Output 'PASS durable scheduler plan is short-lived, interactive, version-pinned, and leased.'
} finally {
    if (Test-Path -LiteralPath $fixtureDirectory) {
        Remove-Item -LiteralPath $fixtureDirectory -Recurse -Force
    }
    if ($directEvidenceRoot -and (Test-Path -LiteralPath $directEvidenceRoot)) {
        Remove-Item -LiteralPath $directEvidenceRoot -Recurse -Force
    }
    if ($runnerEvidenceRoot -and (Test-Path -LiteralPath $runnerEvidenceRoot)) {
        Remove-Item -LiteralPath $runnerEvidenceRoot -Recurse -Force
    }
}
