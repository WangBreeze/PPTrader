param([string]$OutputDirectory = $PSScriptRoot)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class H1Window {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
}
'@

function Find-Element([IntPtr]$handle, [string]$name) {
    $root = [System.Windows.Automation.AutomationElement]::FromHandle($handle)
    $condition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::NameProperty, $name)
    return $root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
}

function Set-Value([IntPtr]$handle, [string]$name, [string]$value) {
    $element = Find-Element $handle $name
    if ($null -eq $element) { throw "找不到输入控件：$name" }
    $pattern = $element.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
    $pattern.SetValue($value)
}

function Invoke-Element([IntPtr]$handle, [string]$name) {
    $element = Find-Element $handle $name
    if ($null -eq $element) { throw "找不到操作控件：$name" }
    $pattern = $element.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
    $pattern.Invoke()
}

function Save-Window([IntPtr]$handle, [string]$path) {
    $rect = New-Object H1Window+RECT
    [H1Window]::GetWindowRect($handle, [ref]$rect) | Out-Null
    $bitmap = New-Object System.Drawing.Bitmap(($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    try { [H1Window]::PrintWindow($handle, $hdc, 3) | Out-Null }
    finally { $graphics.ReleaseHdc($hdc); $graphics.Dispose() }
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

function Wheel-Page([IntPtr]$handle, [int]$count) {
    [H1Window]::SetForegroundWindow($handle) | Out-Null
    $rect = New-Object H1Window+RECT
    [H1Window]::GetWindowRect($handle, [ref]$rect) | Out-Null
    [H1Window]::SetCursorPos(($rect.Right - 18), ($rect.Top + 500)) | Out-Null
    for ($index = 0; $index -lt $count; ++$index) {
        [H1Window]::mouse_event(0x0800, 0, 0, [uint32]::MaxValue - 119, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 60
    }
}

function Wheel-PageUp([IntPtr]$handle, [int]$count) {
    [H1Window]::SetForegroundWindow($handle) | Out-Null
    $rect = New-Object H1Window+RECT
    [H1Window]::GetWindowRect($handle, [ref]$rect) | Out-Null
    [H1Window]::SetCursorPos(($rect.Right - 18), ($rect.Top + 500)) | Out-Null
    for ($index = 0; $index -lt $count; ++$index) {
        [H1Window]::mouse_event(0x0800, 0, 0, 120, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 40
    }
}

function Click-Window([IntPtr]$handle, [int]$x, [int]$y) {
    $rect = New-Object H1Window+RECT
    [H1Window]::GetWindowRect($handle, [ref]$rect) | Out-Null
    [H1Window]::SetCursorPos(($rect.Left + $x), ($rect.Top + $y)) | Out-Null
    [H1Window]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    [H1Window]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
}

function Drag-Scrollbar([IntPtr]$handle, [double]$fromFraction, [double]$toFraction) {
    $rect = New-Object H1Window+RECT
    [H1Window]::GetWindowRect($handle, [ref]$rect) | Out-Null
    $height = $rect.Bottom - $rect.Top
    $x = $rect.Right - 13
    [H1Window]::SetCursorPos($x, ($rect.Top + [int]($height * $fromFraction))) | Out-Null
    [H1Window]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    [H1Window]::SetCursorPos($x, ($rect.Top + [int]($height * $toFraction))) | Out-Null
    Start-Sleep -Milliseconds 200
    [H1Window]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
}

$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $projectRoot 'out\build\vs2022-x64\Debug\PPTrader.exe'
$stdout = Join-Path $OutputDirectory 'runtime.stdout.log'
$stderr = Join-Path $OutputDirectory 'runtime.stderr.log'
$env:PPTRADER_AUTOSTART_LIVE = '0'
$strategyTabName = [regex]::Unescape('\u6307\u6807\u4e0e\u7b56\u7565')
$endpointName = 'NAS ' + [regex]::Unescape('\u6570\u636e\u8282\u70b9\u5730\u5740')
$applyName = [regex]::Unescape('\u5e94\u7528')
$symbolName = 'NAS Tick A ' + [regex]::Unescape('\u80a1\u4ee3\u7801')
$startDateName = 'NAS catalog ' + [regex]::Unescape('\u8d77\u59cb\u65e5\u671f')
$endDateName = 'NAS catalog ' + [regex]::Unescape('\u7ed3\u675f\u65e5\u671f')
$catalogName = [regex]::Unescape('\u67e5\u8be2') + ' catalog'
$syncAllName = [regex]::Unescape('\u540c\u6b65\u5168\u90e8')
$cancelName = [regex]::Unescape('\u53d6\u6d88')
$process = Start-Process -FilePath $exe -WorkingDirectory $projectRoot -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
try {
    for ($index = 0; $index -lt 100 -and $process.MainWindowHandle -eq 0; ++$index) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    $handle = $process.MainWindowHandle
    if ($handle -eq 0) { throw 'PPTrader 主窗口未出现' }
    [H1Window]::MoveWindow($handle, -2500, 40, 1616, 1039, $true) | Out-Null
    [H1Window]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Seconds 2
    Click-Window $handle 433 63
    Start-Sleep -Seconds 2
    Wheel-Page $handle 12
    Start-Sleep -Seconds 2
    Save-Window $handle (Join-Path $OutputDirectory 'nas-scroll-debug.png')
    Set-Value $handle $endpointName 'http://127.0.0.1:18765/'
    Invoke-Element $handle $applyName
    Set-Value $handle $symbolName '600519'
    Set-Value $handle $startDateName '2026-08-05'
    Set-Value $handle $endDateName '2026-08-07'
    Invoke-Element $handle $catalogName
    Start-Sleep -Seconds 4
    Invoke-Element $handle $syncAllName
    Start-Sleep -Seconds 25
    Drag-Scrollbar $handle 0.84 0.40
    Start-Sleep -Seconds 2
    Save-Window $handle (Join-Path $OutputDirectory 'nas-three-day-complete-1600x1000-secondary.png')
    [H1Window]::MoveWindow($handle, -2500, 40, 1136, 799, $true) | Out-Null
    Drag-Scrollbar $handle 0.84 0.45
    Start-Sleep -Seconds 2
    Save-Window $handle (Join-Path $OutputDirectory 'nas-three-day-complete-1120x760-secondary.png')
} finally {
    if (-not $process.HasExited) { $process.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 500 }
    if (-not $process.HasExited) { $process.Kill() }
}
