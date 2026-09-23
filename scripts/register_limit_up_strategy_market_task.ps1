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
    [string]$TaskName = '',
    [ValidateRange(30, 300)]
    [int]$LaunchLeadSeconds = 60,
    [ValidateRange(8, 12)]
    [int]$ExecutionTimeLimitHours = 8,
    [string]$NetworkApprovalReference = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'artifacts'))
$orchestrator = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot `
    'start_limit_up_strategy_market_window.ps1'))
$runner = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot `
    'start_realtime_multiwindow_longrun.ps1'))
$resolvedPlan = [System.IO.Path]::GetFullPath($StrategyStudyPlanPath)
$resolvedEvidence = [System.IO.Path]::GetFullPath($EvidenceRoot)
$resolvedExecutable = [System.IO.Path]::GetFullPath($ApplicationExecutable)
$resolvedQtRoot = [System.IO.Path]::GetFullPath($QtRuntimeRoot)
$qtCore = Join-Path $resolvedQtRoot 'bin\Qt6Core.dll'
$platformPlugin = Join-Path $resolvedQtRoot 'plugins\platforms\qwindows.dll'

if (-not $resolvedEvidence.StartsWith(
        $artifactRoot.TrimEnd('\') + '\',
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Evidence root must be inside the repository artifacts directory.'
}
foreach ($required in @(
        $orchestrator, $runner, $resolvedPlan, $resolvedExecutable, $qtCore,
        $platformPlugin)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing durable scheduler dependency: $required"
    }
}

$strategyPlan = Get-Content -LiteralPath $resolvedPlan -Raw -Encoding UTF8 |
    ConvertFrom-Json
$targetDay = [datetime]::ParseExact(
    [string]$strategyPlan.targetTradingDay, 'yyyy-MM-dd',
    [System.Globalization.CultureInfo]::InvariantCulture)
if ($targetDay.Date -ne $TradingDay.Date) {
    throw 'Strategy plan trading day does not match the scheduled trading day.'
}

if ([string]::IsNullOrWhiteSpace($TaskName)) {
    $TaskName = 'PPTrader-LimitUp-' + $TradingDay.ToString('yyyyMMdd')
}
if ($TaskName -notmatch '^PPTrader-[A-Za-z0-9-]{1,80}$') {
    throw 'TaskName must use the PPTrader- prefix and safe ASCII characters.'
}

$preflightAt = $TradingDay.Date.AddHours(9).AddMinutes(25)
$scheduledAt = $preflightAt.AddSeconds(-$LaunchLeadSeconds)
if (-not $DryRun -and (Get-Date) -ge $scheduledAt) {
    throw 'The durable scheduler registration deadline has already passed.'
}
if (-not $DryRun -and [string]::IsNullOrWhiteSpace(
        $NetworkApprovalReference)) {
    throw 'Non-dry-run registration requires an explicit network approval reference.'
}

function Quote-TaskArgument([string]$value) {
    if ($value.Contains('"')) {
        throw 'Scheduled task arguments cannot contain quotation marks.'
    }
    return '"' + $value + '"'
}

$powershell = Join-Path $env:SystemRoot `
    'System32\WindowsPowerShell\v1.0\powershell.exe'
$planSha256 = (Get-FileHash -LiteralPath $resolvedPlan -Algorithm SHA256).Hash
$orchestratorSha256 = (Get-FileHash -LiteralPath $orchestrator `
    -Algorithm SHA256).Hash
$runnerSha256 = (Get-FileHash -LiteralPath $runner -Algorithm SHA256).Hash
$applicationSha256 = (Get-FileHash -LiteralPath $resolvedExecutable `
    -Algorithm SHA256).Hash
$arguments = @(
    '-NoProfile',
    '-NonInteractive',
    '-ExecutionPolicy', 'Bypass',
    '-File', (Quote-TaskArgument $orchestrator),
    '-TradingDay', (Quote-TaskArgument $TradingDay.ToString('yyyy-MM-dd')),
    '-StrategyStudyPlanPath', (Quote-TaskArgument $resolvedPlan),
    '-EvidenceRoot', (Quote-TaskArgument $resolvedEvidence),
    '-ApplicationExecutable', (Quote-TaskArgument $resolvedExecutable),
    '-QtRuntimeRoot', (Quote-TaskArgument $resolvedQtRoot),
    '-SchedulerTaskName', (Quote-TaskArgument $TaskName),
    '-ExpectedStrategyPlanSha256', (Quote-TaskArgument $planSha256),
    '-ExpectedOrchestratorSha256', (Quote-TaskArgument $orchestratorSha256),
    '-ExpectedRunnerSha256', (Quote-TaskArgument $runnerSha256),
    '-ExpectedApplicationSha256', (Quote-TaskArgument $applicationSha256),
    '-MaximumDirectLaunchLeadMinutes', '10'
) -join ' '

$qtVersion = [System.Diagnostics.FileVersionInfo]::GetVersionInfo(
    $qtCore).FileVersion
$plan = [ordered]@{
    durableScheduler = $true
    scheduler = 'Windows Task Scheduler'
    taskName = $TaskName
    logonType = 'Interactive'
    scheduledAt = $scheduledAt.ToString('o')
    preflightAt = $preflightAt.ToString('o')
    leadSeconds = $LaunchLeadSeconds
    executionTimeLimitHours = $ExecutionTimeLimitHours
    directLaunchMaximumLeadMinutes = 10
    orchestrator = $orchestrator
    orchestratorSha256 = $orchestratorSha256
    runner = $runner
    runnerSha256 = $runnerSha256
    strategyStudyPlanSha256 = $planSha256
    applicationExecutable = $resolvedExecutable
    applicationSha256 = $applicationSha256
    qtRuntimeRoot = $resolvedQtRoot
    qtCorePath = $qtCore
    qtCoreVersion = $qtVersion
    strategyStudyPlanPath = $resolvedPlan
    evidenceRoot = $resolvedEvidence
    catchUpAfterMissedStart = $false
    networkApprovalReference = $NetworkApprovalReference
    networkEnvironment = 'interactive scheduled task; no NAS credentials inherited by PPTrader launcher'
}

if ($DryRun) {
    $plan | ConvertTo-Json -Depth 5
    exit 0
}

Import-Module ScheduledTasks -ErrorAction Stop
if (Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue) {
    throw "Scheduled task already exists: $TaskName"
}
$action = New-ScheduledTaskAction -Execute $powershell -Argument $arguments `
    -WorkingDirectory $repositoryRoot
$trigger = New-ScheduledTaskTrigger -Once -At $scheduledAt
$principal = New-ScheduledTaskPrincipal `
    -UserId ([System.Security.Principal.WindowsIdentity]::GetCurrent().Name) `
    -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet `
    -ExecutionTimeLimit (New-TimeSpan -Hours $ExecutionTimeLimitHours) `
    -MultipleInstances IgnoreNew -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
    -Principal $principal -Settings $settings -Description (
        'PPTrader durable real-market acceptance launcher. ' +
        'Starts one minute before the gated preflight.') | Out-Null

[System.IO.Directory]::CreateDirectory($resolvedEvidence) | Out-Null
$plan['registeredAt'] = (Get-Date).ToString('o')
$plan | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $resolvedEvidence `
        'scheduled-task.json') -Encoding utf8
$plan | ConvertTo-Json -Depth 5
