param([string]$OutputDirectory = $PSScriptRoot)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RealtimeRouterWindow {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
}
'@

function Save-Window([IntPtr]$handle, [string]$path) {
    $rect = New-Object RealtimeRouterWindow+RECT
    if (-not [RealtimeRouterWindow]::GetWindowRect($handle, [ref]$rect)) { throw '无法读取窗口矩形' }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    try {
        if (-not [RealtimeRouterWindow]::PrintWindow($handle, $hdc, 3)) { throw 'PrintWindow 失败' }
    } finally {
        $graphics.ReleaseHdc($hdc)
        $graphics.Dispose()
    }
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    [PSCustomObject]@{ Path = $path; Width = $width; Height = $height }
}

$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $projectRoot 'out\build\vs2022-x64\Debug\PPTrader.exe'
$env:PPTRADER_AUTOSTART_LIVE = '1'
$env:PPTRADER_PYTHON = Join-Path $projectRoot '.venv\Scripts\python.exe'
$env:PPTRADER_TDX_PRIMARY_SERVER = '127.0.0.1:1'
$env:PPTRADER_TDX_BACKUP_SERVER = '180.153.18.172:80'
Remove-Item Env:PPTRADER_TDX_MAX_BATCHES -ErrorAction SilentlyContinue
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
    [RealtimeRouterWindow]::MoveWindow($handle, -2500, 40, 1616, 1039, $true) | Out-Null
    [RealtimeRouterWindow]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Seconds 65
    Save-Window $handle (Join-Path $OutputDirectory 'failover-1600x1000-secondary.png')
    [RealtimeRouterWindow]::MoveWindow($handle, -2500, 40, 1136, 799, $true) | Out-Null
    Start-Sleep -Seconds 2
    Save-Window $handle (Join-Path $OutputDirectory 'failover-1120x760-secondary.png')
} finally {
    if (-not $process.HasExited) { $process.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 500 }
    if (-not $process.HasExited) { $process.Kill() }
}
