param(
    [string]$OutputDirectory = $PSScriptRoot,
    [switch]$CacheOnly
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class H1BWindow {
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

function Scroll-IntoView([IntPtr]$handle, [string]$name) {
    $element = Find-Element $handle $name
    if ($null -eq $element) { throw "找不到滚动目标：$name" }
    $pattern = $null
    if ($element.TryGetCurrentPattern(
            [System.Windows.Automation.ScrollItemPattern]::Pattern, [ref]$pattern)) {
        $pattern.ScrollIntoView()
    }
    $element.SetFocus()
}

function Set-PageScroll([IntPtr]$handle, [double]$percent) {
    $root = [System.Windows.Automation.AutomationElement]::FromHandle($handle)
    $condition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::IsScrollPatternAvailableProperty, $true)
    $elements = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $condition)
    $best = $null
    $bestArea = 0
    foreach ($element in $elements) {
        $rect = $element.Current.BoundingRectangle
        $area = $rect.Width * $rect.Height
        $pattern = $element.GetCurrentPattern([System.Windows.Automation.ScrollPattern]::Pattern)
        if ($pattern.Current.VerticallyScrollable -and $area -gt $bestArea) {
            $best = $pattern
            $bestArea = $area
        }
    }
    if ($null -eq $best) { throw '找不到策略页纵向滚动容器' }
    $best.SetScrollPercent([System.Windows.Automation.ScrollPattern]::NoScroll, $percent)
}

function Wheel-Page([IntPtr]$handle, [int]$count) {
    $rect = New-Object H1BWindow+RECT
    [H1BWindow]::GetWindowRect($handle, [ref]$rect) | Out-Null
    [H1BWindow]::SetCursorPos(($rect.Left + [int](($rect.Right - $rect.Left) / 2)),
                             ($rect.Top + 500)) | Out-Null
    for ($index = 0; $index -lt $count; ++$index) {
        [H1BWindow]::mouse_event(0x0800, 0, 0, [uint32]::MaxValue - 119, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 60
    }
}

function Page-ScrollbarDown([IntPtr]$handle, [int]$count) {
    $rect = New-Object H1BWindow+RECT
    [H1BWindow]::GetWindowRect($handle, [ref]$rect) | Out-Null
    for ($index = 0; $index -lt $count; ++$index) {
        [H1BWindow]::SetCursorPos(($rect.Right - 12), ($rect.Bottom - 105)) | Out-Null
        [H1BWindow]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
        [H1BWindow]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 180
    }
}

function Drag-Scrollbar([IntPtr]$handle, [double]$fromFraction, [double]$toFraction) {
    $rect = New-Object H1BWindow+RECT
    [H1BWindow]::GetWindowRect($handle, [ref]$rect) | Out-Null
    $height = $rect.Bottom - $rect.Top
    $x = $rect.Right - 13
    [H1BWindow]::SetCursorPos($x, ($rect.Top + [int]($height * $fromFraction))) | Out-Null
    [H1BWindow]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    [H1BWindow]::SetCursorPos($x, ($rect.Top + [int]($height * $toFraction))) | Out-Null
    Start-Sleep -Milliseconds 200
    [H1BWindow]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
}

function Save-Window([IntPtr]$handle, [string]$path) {
    $rect = New-Object H1BWindow+RECT
    [H1BWindow]::GetWindowRect($handle, [ref]$rect) | Out-Null
    $bitmap = New-Object System.Drawing.Bitmap(($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    try { [H1BWindow]::PrintWindow($handle, $hdc, 3) | Out-Null }
    finally { $graphics.ReleaseHdc($hdc); $graphics.Dispose() }
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $projectRoot 'out\build\vs2022-x64\Debug\PPTrader.exe'
$stdout = Join-Path $OutputDirectory 'runtime.stdout.log'
$stderr = Join-Path $OutputDirectory 'runtime.stderr.log'
$env:PPTRADER_AUTOSTART_LIVE = '0'
$endpointName = 'NAS ' + [regex]::Unescape('\u6570\u636e\u8282\u70b9\u5730\u5740')
$manifestName = 'NAS ' + [regex]::Unescape('\u53d7\u63a7') + ' manifest A ' + [regex]::Unescape('\u80a1\u4ee3\u7801\u6e05\u5355')
$startDateName = 'NAS catalog ' + [regex]::Unescape('\u8d77\u59cb\u65e5\u671f')
$endDateName = 'NAS catalog ' + [regex]::Unescape('\u7ed3\u675f\u65e5\u671f')
$applyName = [regex]::Unescape('\u5e94\u7528')
$queryName = [regex]::Unescape('\u67e5\u8be2') + ' manifest'
$symbols = '000001 000002 000006 000007 000008 000009 000010 000011 000012 000014'
$process = Start-Process -FilePath $exe -WorkingDirectory $projectRoot -PassThru `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr
try {
    for ($index = 0; $index -lt 100 -and $process.MainWindowHandle -eq 0; ++$index) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    $handle = $process.MainWindowHandle
    if ($handle -eq 0) { throw 'PPTrader 主窗口未出现' }
    [H1BWindow]::MoveWindow($handle, -2500, 40, 1616, 1039, $true) | Out-Null
    [H1BWindow]::SetForegroundWindow($handle) | Out-Null
    Start-Sleep -Seconds 2
    [H1BWindow]::SetCursorPos(-2060, 104) | Out-Null
    [H1BWindow]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    [H1BWindow]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Seconds 2
    Wheel-Page $handle 13
    Start-Sleep -Seconds 2
    if ($CacheOnly) {
        $refreshName = [regex]::Unescape('\u5237\u65b0\u7f13\u5b58\u7edf\u8ba1')
        Scroll-IntoView $handle $refreshName
        Invoke-Element $handle $refreshName
        Start-Sleep -Seconds 2
        Save-Window $handle (Join-Path $OutputDirectory 'h1c-cache-1600x1000-secondary.png')
        [H1BWindow]::MoveWindow($handle, -2500, 40, 1136, 799, $true) | Out-Null
        Start-Sleep -Seconds 2
        Scroll-IntoView $handle $refreshName
        Save-Window $handle (Join-Path $OutputDirectory 'h1c-cache-1120x760-secondary.png')
        return
    }
    Set-Value $handle $endpointName 'http://127.0.0.1:28765/'
    Invoke-Element $handle $applyName
    Set-Value $handle $startDateName '2026-08-07'
    Set-Value $handle $endDateName '2026-08-07'
    Set-Value $handle $manifestName $symbols
    Invoke-Element $handle $queryName
    Start-Sleep -Seconds 5
    Drag-Scrollbar $handle 0.84 0.40
    Start-Sleep -Seconds 2
    Save-Window $handle (Join-Path $OutputDirectory 'manifest-10-1600x1000-secondary.png')
    [H1BWindow]::MoveWindow($handle, -2500, 40, 1136, 799, $true) | Out-Null
    Start-Sleep -Seconds 2
    Save-Window $handle (Join-Path $OutputDirectory 'manifest-10-1120x760-secondary.png')
} finally {
    if (-not $process.HasExited) { $process.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 500 }
    if (-not $process.HasExited) { $process.Kill() }
}
