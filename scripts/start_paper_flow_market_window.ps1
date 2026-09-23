param([Parameter(Mandatory=$true)][string]$ExpectedBinarySha256)
$ErrorActionPreference='Stop'
$root='I:\workspace\PPTrader\artifacts\paper-flow-2026-09-08'
$runner='I:\workspace\PPTrader\scripts\run_paper_flow_acceptance.ps1'
$status=Join-Path $root 'market-orchestrator.json'
function Write-State([string]$stage,[string]$detail='') {
    @{stage=$stage;detail=$detail;orchestratorPid=$PID;at=(Get-Date).ToString('o');startAt='2026-09-08T09:30:00+08:00';endAt='2026-09-08T15:31:00+08:00';expectedBinarySha256=$ExpectedBinarySha256} | ConvertTo-Json | Set-Content -LiteralPath $status -Encoding utf8
}
if (Test-Path -LiteralPath $status) { throw '市场窗口编排已存在，不重复启动或覆盖既有状态' }
try {
    Write-State 'waiting-for-09:30'
    $start=[DateTimeOffset]::Parse('2026-09-08T09:30:00+08:00')
    while ([DateTimeOffset]::Now -lt $start) { Start-Sleep -Seconds 10 }
    Write-State 'one-symbol-preflight'
    & $runner -Mode preflight -OutputDirectory (Join-Path $root 'market-preflight') -EndAt '2026-09-08T09:35:00+08:00' -ExpectedBinarySha256 $ExpectedBinarySha256
    $gate=Get-Content -Raw -LiteralPath (Join-Path $root 'market-preflight\report.json') | ConvertFrom-Json
    if (-not $gate.passed) { throw '同二进制单品种预检未通过，禁止正式段' }
    Write-State 'live-paper-running'
    & $runner -Mode live -OutputDirectory (Join-Path $root 'market-live') -Candidates 2 -EndAt '2026-09-08T15:31:00+08:00' -ExpectedBinarySha256 $ExpectedBinarySha256
    Write-State 'completed'
} catch {
    Write-State 'failed' $_.Exception.Message
    throw
}
