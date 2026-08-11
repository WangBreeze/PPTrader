param([string]$OutputDirectory = $PSScriptRoot)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NativeWindow {
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
    [NativeWindow]::PostMessage($handle, 0x0201, [IntPtr]1, $point) | Out-Null
    [NativeWindow]::PostMessage($handle, 0x0202, [IntPtr]0, $point) | Out-Null
}

function Send-Wheel([IntPtr]$handle, [bool]$up, [int]$count) {
    # Keep the pointer in the page margin so nested lists/charts cannot consume wheel input.
    [NativeWindow]::SetCursorPos(-2488, 650) | Out-Null
    [NativeWindow]::SetForegroundWindow($handle) | Out-Null
    $wheel = if ($up) { 120 } else { -120 }
    for ($i = 0; $i -lt $count; ++$i) {
        [NativeWindow]::Wheel($wheel)
        Start-Sleep -Milliseconds 70
    }
}

function Save-Window([IntPtr]$handle, [string]$path) {
    $rect = New-Object NativeWindow+RECT
    if (-not [NativeWindow]::GetWindowRect($handle, [ref]$rect)) { throw '无法读取窗口矩形' }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    try {
        if (-not [NativeWindow]::PrintWindow($handle, $hdc, 3)) { throw 'PrintWindow 失败' }
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
$env:PPTRADER_AUTOSTART_LIVE = '0'
$process = Start-Process -FilePath $exe -WorkingDirectory $projectRoot -PassThru
try {
    for ($i = 0; $i -lt 80 -and $process.MainWindowHandle -eq 0; ++$i) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    $handle = $process.MainWindowHandle
    if ($handle -eq 0) { throw 'PPTrader 主窗口未出现' }

    [NativeWindow]::MoveWindow($handle, -2500, 40, 1616, 1039, $true) | Out-Null
    [NativeWindow]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Seconds 2
    Send-ClientClick $handle 430 32
    Start-Sleep -Milliseconds 700
    Send-Wheel $handle $false 4
    Start-Sleep -Seconds 1
    Save-Window $handle (Join-Path $OutputDirectory 'batch-plan-1600x1000-secondary.png')

    Send-Wheel $handle $true 80
    [NativeWindow]::MoveWindow($handle, -2500, 40, 1136, 799, $true) | Out-Null
    Start-Sleep -Seconds 1
    Send-Wheel $handle $false 8
    Start-Sleep -Seconds 1
    Save-Window $handle (Join-Path $OutputDirectory 'batch-plan-1120x760-secondary.png')
} finally {
    if (-not $process.HasExited) { $process.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 500 }
    if (-not $process.HasExited) { $process.Kill() }
}
