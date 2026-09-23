param(
    [ValidateSet('replay','preflight','live')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [int]$Candidates = 1,
    [string]$EndAt = '2026-09-08T08:55:00+08:00',
    [string]$ExpectedBinarySha256 = '',
    [string]$StartAt = ''
)
$ErrorActionPreference = 'Stop'
$repo = 'I:\workspace\PPTrader'
$binary = Join-Path $repo 'out\build\vs2022-x64\Release\PPTrader.exe'
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (-not $output.StartsWith($repo + '\artifacts\paper-flow-2026-09-08\', [StringComparison]::OrdinalIgnoreCase)) {
    throw '输出必须位于本次隔离证据目录'
}
if (Test-Path -LiteralPath (Join-Path $output 'launch.json')) { throw '本次输出已使用，禁止覆盖或重复启动' }
New-Item -ItemType Directory -Path $output -Force | Out-Null
if ($StartAt) {
    $start = [DateTimeOffset]::Parse($StartAt)
    while ([DateTimeOffset]::Now -lt $start) { Start-Sleep -Seconds 10 }
}
$end = [DateTimeOffset]::Parse($EndAt)
if ([DateTimeOffset]::Now -ge $end) { throw '验收窗口已经结束' }
if ($Mode -eq 'replay' -and [DateTimeOffset]::Now -ge [DateTimeOffset]::Parse('2026-09-08T08:59:00+08:00')) { throw '盘前硬停止线已到' }
if ($Mode -eq 'live' -and [DateTimeOffset]::Now -lt [DateTimeOffset]::Parse('2026-09-08T09:30:00+08:00')) { throw '未到真实行情验收窗口' }
$active = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq 'PPTrader.exe' -or ($_.Name -match '^python(w)?\.exe$' -and $_.CommandLine -match 'mootdx_bridge.py')
})
if ($active.Count) { throw '已有 PPTrader 或 TDX 桥，禁止重叠启动' }
$hash = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
if ($ExpectedBinarySha256 -and $hash -ne $ExpectedBinarySha256) { throw '二进制与已验证版本不一致，暂停而非自动重建' }
$env:PPTRADER_PYTHON = Join-Path $repo '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $env:PPTRADER_PYTHON)) { throw '未找到既有 Python 环境' }
$env:PPTRADER_PAPER_FLOW_MODE = $Mode
$env:PPTRADER_PAPER_FLOW_OUTPUT = $output
$env:PPTRADER_PAPER_FLOW_SOURCE = Join-Path $repo 'artifacts\paper-flow-2026-09-08\source'
$env:PPTRADER_PAPER_FLOW_CANDIDATES = [string]$Candidates
$env:PPTRADER_PAPER_FLOW_END = $EndAt
$env:PPTRADER_AUTOSTART_LIVE = '0'
Remove-Item Env:PPTRADER_ACCEPTANCE_DISABLE_ACCESSIBILITY -ErrorAction SilentlyContinue
$app = Start-Process -FilePath $binary -ArgumentList '--paper-flow-acceptance' -WorkingDirectory $repo -WindowStyle Normal -PassThru -RedirectStandardOutput (Join-Path $output 'stdout.log') -RedirectStandardError (Join-Path $output 'stderr.log')
$appStart = $app.StartTime
@{mode=$Mode;processId=$app.Id;startedAt=$appStart.ToString('o');binary=$binary;binarySha256=$hash;endAt=$EndAt;candidates=$Candidates;networkEnvironment='explicitly-approved parent, inherited child';source='NAS files downloaded before replay; source clock unchanged';realOrders=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'launch.json') -Encoding utf8
$forced = $false
while (-not $app.HasExited) {
    $app.Refresh()
    if ($app.HasExited) { break }
    $row = @{at=(Get-Date).ToString('o');pid=$app.Id;privateMB=$app.PrivateMemorySize64/1MB;workingSetMB=$app.WorkingSet64/1MB;handles=$app.HandleCount;threads=$app.Threads.Count;cpuSeconds=$app.TotalProcessorTime.TotalSeconds}
    $row | ConvertTo-Json -Compress | Add-Content -LiteralPath (Join-Path $output 'resources.jsonl') -Encoding utf8
    if ([DateTimeOffset]::Now -gt $end.AddSeconds(5)) {
        $exact = Get-Process -Id $app.Id -ErrorAction SilentlyContinue
        if ($exact -and $exact.Path -eq $binary -and $exact.StartTime -eq $appStart) { $exact.Kill(); $forced=$true }
    }
    Start-Sleep -Milliseconds 500
}
$app.WaitForExit()
@{processId=$app.Id;exitedAt=(Get-Date).ToString('o');exitCode=$app.ExitCode;forced=$forced} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'exit.json') -Encoding utf8
if ($forced -or $app.ExitCode -ne 0) { throw "流程未通过或退出异常，ExitCode=$($app.ExitCode)，请读取证据，不自动重跑" }
