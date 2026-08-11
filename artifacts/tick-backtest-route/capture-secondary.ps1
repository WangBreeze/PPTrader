param(
    [string]$OutputDirectory = $PSScriptRoot,
    [switch]$RunBacktest
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TickBacktestWindow {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
}
'@

function Click-Window([IntPtr]$handle, [int]$x, [int]$y) {
    $rect = New-Object TickBacktestWindow+RECT
    [TickBacktestWindow]::GetWindowRect($handle, [ref]$rect) | Out-Null
    [TickBacktestWindow]::SetCursorPos($rect.Left + $x, $rect.Top + $y) | Out-Null
    [TickBacktestWindow]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    [TickBacktestWindow]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
}

function Save-Window([IntPtr]$handle, [string]$path) {
    $rect = New-Object TickBacktestWindow+RECT
    if (-not [TickBacktestWindow]::GetWindowRect($handle, [ref]$rect)) { throw 'Cannot read the application window bounds.' }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    try {
        if (-not [TickBacktestWindow]::PrintWindow($handle, $hdc, 3)) { throw 'PrintWindow failed.' }
    } finally {
        $graphics.ReleaseHdc($hdc)
        $graphics.Dispose()
    }
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

function Find-AutomationElement([IntPtr]$handle, [string]$name) {
    $root = [System.Windows.Automation.AutomationElement]::FromHandle($handle)
    $condition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::NameProperty, $name)
    return $root.FindFirst(
        [System.Windows.Automation.TreeScope]::Descendants, $condition)
}

function Set-AutomationValue([IntPtr]$handle, [string]$name, [string]$value) {
    $element = Find-AutomationElement $handle $name
    if ($null -eq $element) { throw "Accessible element not found: $name" }
    $pattern = $element.GetCurrentPattern(
        [System.Windows.Automation.ValuePattern]::Pattern)
    $pattern.SetValue($value)
}

function Invoke-AutomationElement([IntPtr]$handle, [string]$name) {
    $element = Find-AutomationElement $handle $name
    if ($null -eq $element) { throw "Accessible element not found: $name" }
    $pattern = $element.GetCurrentPattern(
        [System.Windows.Automation.InvokePattern]::Pattern)
    $pattern.Invoke()
}

$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $projectRoot 'out\build\vs2022-x64\Debug\PPTrader.exe'
Remove-Item Env:PPTRADER_AUTOSTART_LIVE -ErrorAction SilentlyContinue
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
    if ($handle -eq 0) { throw 'PPTrader main window did not appear.' }
    [TickBacktestWindow]::MoveWindow($handle, -2500, 40, 1616, 1039, $true) | Out-Null
    [TickBacktestWindow]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Seconds 3
    # MoveWindow includes the native 8 px border and 31 px title-frame offset.
    Click-Window $handle 433 63
    Start-Sleep -Seconds 3
    Save-Window $handle (Join-Path $OutputDirectory 'strategy-tick-catalog-1600x1000-secondary.png')
    if ($RunBacktest) {
        $barCountName = [regex]::Unescape('K \u7ebf\u6570\u91cf')
        $runBacktestName = [regex]::Unescape('\u8fd0\u884c\u56de\u6d4b')
        Set-AutomationValue $handle $barCountName '200'
        Start-Sleep -Milliseconds 300
        Invoke-AutomationElement $handle $runBacktestName
        Start-Sleep -Seconds 3
        Save-Window $handle (Join-Path $OutputDirectory 'strategy-tick-backtest-result-1600x1000-secondary.png')
    }
    [TickBacktestWindow]::MoveWindow($handle, -2500, 40, 1136, 799, $true) | Out-Null
    Start-Sleep -Seconds 2
    Save-Window $handle (Join-Path $OutputDirectory 'strategy-tick-catalog-1120x760-secondary.png')
} finally {
    if (-not $process.HasExited) { $process.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 500 }
    if (-not $process.HasExited) { $process.Kill() }
}
