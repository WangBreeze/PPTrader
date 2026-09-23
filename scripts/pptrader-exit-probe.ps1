param(
    [string]$Executable = (Join-Path $PSScriptRoot '..\out\build\vs2022-x64\Debug\PPTrader.exe'),
    [int]$TimeoutSeconds = 60,
    [ValidateRange(1, 100)]
    [int]$Repeat = 1
)

$resolved = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
for ($iteration = 1; $iteration -le $Repeat; ++$iteration) {
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $resolved
    $startInfo.Arguments = '--exit-probe-native'
    $startInfo.UseShellExecute = $false
    $startInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    # 只修改测试子进程环境；即使调用者启用自动行情，退出探针也保持离线。
    $startInfo.EnvironmentVariables['PPTRADER_AUTOSTART_LIVE'] = '0'
    $process = [System.Diagnostics.Process]::Start($startInfo)
    try {
        do {
            if ($process.WaitForExit(200)) { break }
        } while ($timer.Elapsed.TotalSeconds -lt [Math]::Max(1, $TimeoutSeconds))

        if ($process.HasExited) {
            if ($null -eq $process.ExitCode -or $process.ExitCode -ne 0) {
                Write-Error "FAIL: GUI lifecycle failed (PID $($process.Id), exit code $($process.ExitCode))."
                exit 1
            }
            Write-Output "PASS: native close $iteration/$Repeat (PID $($process.Id), startup+exit $($timer.ElapsedMilliseconds) ms, exit code 0)."
            continue
        }

        $live = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
        if ($null -ne $live -and $live.Path -eq $resolved -and $live.StartTime -eq $process.StartTime) {
            Write-Error "FAIL: PPTrader PID $($process.Id) remained alive after $TimeoutSeconds seconds."
            Stop-Process -Id $process.Id -Force
        } else {
            Write-Error "FAIL: probe PID $($process.Id) could not be verified safely."
        }
        exit 1
    } finally {
        if ($null -ne $process) { $process.Dispose() }
    }
}
exit 0
