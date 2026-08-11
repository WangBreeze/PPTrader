param([string]$OutputDirectory = $PSScriptRoot)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HistoricalProviderWindow {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] private static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    public static void Wheel(int delta) { mouse_event(0x0800, 0, 0, unchecked((uint)delta), UIntPtr.Zero); }
}
'@

function Send-ClientClick([IntPtr]$handle, [int]$x, [int]$y) {
    $point = [IntPtr](($y -shl 16) -bor ($x -band 0xffff))
    [HistoricalProviderWindow]::PostMessage($handle, 0x0201, [IntPtr]1, $point) | Out-Null
    [HistoricalProviderWindow]::PostMessage($handle, 0x0202, [IntPtr]0, $point) | Out-Null
}

function Save-Window([IntPtr]$handle, [string]$path) {
    $rect = New-Object HistoricalProviderWindow+RECT
    if (-not [HistoricalProviderWindow]::GetWindowRect($handle, [ref]$rect)) { throw '无法读取窗口矩形' }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    try {
        if (-not [HistoricalProviderWindow]::PrintWindow($handle, $hdc, 3)) { throw 'PrintWindow 失败' }
    } finally {
        $graphics.ReleaseHdc($hdc)
        $graphics.Dispose()
    }
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    [PSCustomObject]@{ Path = $path; Width = $width; Height = $height }
}

function Send-WheelDown([IntPtr]$handle, [int]$count) {
    [HistoricalProviderWindow]::SetCursorPos(-2488, 650) | Out-Null
    [HistoricalProviderWindow]::SetForegroundWindow($handle) | Out-Null
    for ($index = 0; $index -lt $count; ++$index) {
        [HistoricalProviderWindow]::Wheel(-120)
        Start-Sleep -Milliseconds 70
    }
}

$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $projectRoot 'out\build\vs2022-x64\Debug\PPTrader.exe'
$env:PPTRADER_AUTOSTART_LIVE = '0'
$env:PPTRADER_PYTHON = Join-Path $projectRoot '.venv\Scripts\python.exe'
$stdout = Join-Path $OutputDirectory 'runtime.stdout.log'
$stderr = Join-Path $OutputDirectory 'runtime.stderr.log'
$process = Start-Process -FilePath $exe -WorkingDirectory $projectRoot -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
try {
    for ($index = 0; $index -lt 100 -and $process.MainWindowHandle -eq 0; ++$index) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    $handle = $process.MainWindowHandle
    if ($handle -eq 0) { throw 'PPTrader 主窗口未出现' }

    [HistoricalProviderWindow]::MoveWindow($handle, -2500, 40, 1616, 1039, $true) | Out-Null
    [HistoricalProviderWindow]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Seconds 2
    Send-ClientClick $handle 430 32
    Start-Sleep -Seconds 1

    Send-ClientClick $handle 260 604
    [System.Windows.Forms.SendKeys]::SendWait('^a')
    [System.Windows.Forms.SendKeys]::SendWait('300750')
    Start-Sleep -Seconds 1
    Send-ClientClick $handle 210 675
    Start-Sleep -Milliseconds 500
    Send-ClientClick $handle 735 765
    Start-Sleep -Seconds 38
    [HistoricalProviderWindow]::SetCursorPos(-2490, 900) | Out-Null
    Start-Sleep -Seconds 1
    Save-Window $handle (Join-Path $OutputDirectory 'history-provider-cache-reuse-1600x1000-secondary.png')

    [HistoricalProviderWindow]::MoveWindow($handle, -2500, 40, 1136, 799, $true) | Out-Null
    Start-Sleep -Seconds 2
    Send-WheelDown $handle 3
    Start-Sleep -Seconds 1
    Save-Window $handle (Join-Path $OutputDirectory 'history-provider-1120x760-secondary.png')
} finally {
    if (-not $process.HasExited) { $process.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 500 }
    if (-not $process.HasExited) { $process.Kill() }
}
