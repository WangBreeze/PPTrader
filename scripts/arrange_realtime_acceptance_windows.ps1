param(
    [Parameter(Mandatory = $true)]
    [string]$EvidenceDirectory,
    [datetime]$StartAt = (Get-Date),
    [int]$TimeoutSeconds = 180,
    [string]$TemplatePath = ''
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'artifacts'))
$resolvedEvidence = [System.IO.Path]::GetFullPath($EvidenceDirectory)
if (-not $resolvedEvidence.StartsWith(
        $artifactRoot.TrimEnd('\') + '\',
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Evidence directory must be inside the repository artifacts directory.'
}

while ((Get-Date) -lt $StartAt) {
    $remainingSeconds = [Math]::Ceiling(($StartAt - (Get-Date)).TotalSeconds)
    Start-Sleep -Seconds ([Math]::Max(1, [Math]::Min(15, $remainingSeconds)))
}

$launchMetadataPath = Join-Path $resolvedEvidence 'launch-metadata.json'
$deadline = (Get-Date).AddSeconds([Math]::Max(30, $TimeoutSeconds))
while (-not (Test-Path -LiteralPath $launchMetadataPath)) {
    if ((Get-Date) -ge $deadline) {
        throw 'Timed out waiting for launch-metadata.json.'
    }
    Start-Sleep -Seconds 2
}

$launch = Get-Content -LiteralPath $launchMetadataPath -Raw | ConvertFrom-Json
$applicationPid = [int]$launch.applicationPid
$expectedExecutable = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'out\build\vs2022-x64\Release\PPTrader.exe'))
$application = Get-Process -Id $applicationPid -ErrorAction Stop
if (-not [string]::Equals(
        [System.IO.Path]::GetFullPath($application.Path),
        $expectedExecutable,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "PID $applicationPid does not match the expected PPTrader executable."
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class PPTraderWindowLayout {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Point {
        public int X;
        public int Y;
    }

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int maximumCount);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out Rect rect);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out Rect rect);

    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr hWnd, ref Point point);

    [DllImport("user32.dll")]
    public static extern bool ShowWindowAsync(IntPtr hWnd, int command);

    [DllImport("user32.dll")]
    public static extern bool MoveWindow(
        IntPtr hWnd, int x, int y, int width, int height, bool repaint);

    public static IntPtr[] WindowsForProcess(uint targetProcessId) {
        var windows = new List<IntPtr>();
        EnumWindows(delegate(IntPtr hWnd, IntPtr ignored) {
            uint processId;
            GetWindowThreadProcessId(hWnd, out processId);
            if (processId == targetProcessId && IsWindowVisible(hWnd)) {
                windows.Add(hWnd);
            }
            return true;
        }, IntPtr.Zero);
        return windows.ToArray();
    }

    public static string Title(IntPtr hWnd) {
        var text = new StringBuilder(512);
        GetWindowText(hWnd, text, text.Capacity);
        return text.ToString();
    }
}
'@

$childWindows = @()
while ($childWindows.Count -lt 2) {
    if ((Get-Date) -ge $deadline) {
        throw "Timed out waiting for two realtime child windows; found $($childWindows.Count)."
    }
    $childWindows = @(
        [PPTraderWindowLayout]::WindowsForProcess([uint32]$applicationPid) |
            ForEach-Object {
                [PSCustomObject]@{
                    Handle = $_
                    Title = [PPTraderWindowLayout]::Title($_)
                }
            } |
            # Windows PowerShell 5.1 may decode a UTF-8-without-BOM script by
            # using the system code page.  Realtime child titles always contain
            # their six-digit symbol, whereas the main window title does not.
            # Do not use Process.MainWindowHandle here: after a child activates,
            # Windows may report that child as the process main window.
            Where-Object {
                $_.Title.StartsWith('PPTrader') -and
                $_.Title -match '\d{6}'
            } |
            Sort-Object Title
    )
    if ($childWindows.Count -lt 2) { Start-Sleep -Seconds 2 }
}

$templatePlacements = @()
if (-not [string]::IsNullOrWhiteSpace($TemplatePath)) {
    $resolvedTemplate = (Resolve-Path -LiteralPath $TemplatePath).Path
    $template = Get-Content -LiteralPath $resolvedTemplate -Raw | ConvertFrom-Json
    $templatePlacements = @($template.placements)
    if ($templatePlacements.Count -ne 2) {
        throw 'The layout template must contain exactly two child placements.'
    }
}

$screens = @([System.Windows.Forms.Screen]::AllScreens |
    Sort-Object @{ Expression = { if ($_.Primary) { 1 } else { 0 } } }, DeviceName)
if ($templatePlacements.Count -eq 0 -and $screens.Count -lt 2) {
    throw 'Two physical displays are required for dual-window acceptance.'
}

$placements = @()
for ($index = 0; $index -lt 2; ++$index) {
    if ($templatePlacements.Count -eq 2) {
        $target = $templatePlacements[$index]
        $symbol = [string]$target.symbol
        $matches = @($childWindows | Where-Object {
            [string]::IsNullOrWhiteSpace($symbol) -or $_.Title.Contains($symbol)
        })
        if ($matches.Count -ne 1) {
            throw "Expected one child window for template symbol $symbol; found $($matches.Count)."
        }
        $window = $matches[0]
        $targetX = [int]$target.x
        $targetY = [int]$target.y
        $targetWidth = [int]$target.width
        $targetHeight = [int]$target.height
        $requestedScreen = [string]$target.screen
    } else {
        $window = $childWindows[$index]
        $screen = $screens[$index]
        $area = $screen.WorkingArea
        $targetX = $area.X
        $targetY = $area.Y
        $targetWidth = $area.Width
        $targetHeight = $area.Height
        $requestedScreen = $screen.DeviceName
    }
    $outerBefore = New-Object PPTraderWindowLayout+Rect
    $clientBefore = New-Object PPTraderWindowLayout+Rect
    $clientOriginBefore = New-Object PPTraderWindowLayout+Point
    if (-not [PPTraderWindowLayout]::GetWindowRect(
            $window.Handle, [ref]$outerBefore) -or
        -not [PPTraderWindowLayout]::GetClientRect(
            $window.Handle, [ref]$clientBefore) -or
        -not [PPTraderWindowLayout]::ClientToScreen(
            $window.Handle, [ref]$clientOriginBefore)) {
        throw "Unable to resolve native frame metrics for $($window.Title)."
    }
    $leftFrame = $clientOriginBefore.X - $outerBefore.Left
    $topFrame = $clientOriginBefore.Y - $outerBefore.Top
    $rightFrame = $outerBefore.Right -
        ($clientOriginBefore.X + $clientBefore.Right - $clientBefore.Left)
    $bottomFrame = $outerBefore.Bottom -
        ($clientOriginBefore.Y + $clientBefore.Bottom - $clientBefore.Top)
    $outerTargetX = $targetX - $leftFrame
    $outerTargetY = $targetY - $topFrame
    $outerTargetWidth = $targetWidth + $leftFrame + $rightFrame
    $outerTargetHeight = $targetHeight + $topFrame + $bottomFrame

    [void][PPTraderWindowLayout]::ShowWindowAsync($window.Handle, 9)
    Start-Sleep -Milliseconds 150
    if (-not [PPTraderWindowLayout]::MoveWindow(
            $window.Handle, $outerTargetX, $outerTargetY,
            $outerTargetWidth, $outerTargetHeight, $true)) {
        throw "MoveWindow failed for $($window.Title)."
    }
    Start-Sleep -Milliseconds 250
    $rect = New-Object PPTraderWindowLayout+Rect
    if (-not [PPTraderWindowLayout]::GetWindowRect($window.Handle, [ref]$rect)) {
        throw "GetWindowRect failed for $($window.Title)."
    }
    $clientRect = New-Object PPTraderWindowLayout+Rect
    $clientOrigin = New-Object PPTraderWindowLayout+Point
    if (-not [PPTraderWindowLayout]::GetClientRect(
            $window.Handle, [ref]$clientRect) -or
        -not [PPTraderWindowLayout]::ClientToScreen(
            $window.Handle, [ref]$clientOrigin)) {
        throw "Unable to verify client geometry for $($window.Title)."
    }
    $actualScreen = [System.Windows.Forms.Screen]::FromHandle($window.Handle)
    $placements += [PSCustomObject]@{
        handle = ('0x{0:X}' -f $window.Handle.ToInt64())
        title = $window.Title
        requestedScreen = $requestedScreen
        actualScreen = $actualScreen.DeviceName
        screenPrimary = $actualScreen.Primary
        x = $clientOrigin.X
        y = $clientOrigin.Y
        width = $clientRect.Right - $clientRect.Left
        height = $clientRect.Bottom - $clientRect.Top
        requestedGeometry = [PSCustomObject]@{
            x = $targetX
            y = $targetY
            width = $targetWidth
            height = $targetHeight
        }
        nativeGeometry = [PSCustomObject]@{
            x = $rect.Left
            y = $rect.Top
            width = $rect.Right - $rect.Left
            height = $rect.Bottom - $rect.Top
        }
    }
}

[PSCustomObject]@{
    arrangedAt = (Get-Date).ToString('o')
    applicationPid = $applicationPid
    method = if ($templatePlacements.Count -eq 2) {
        'Win32 MoveWindow using captured Windows Snap layout template'
    } else {
        'Win32 MoveWindow to each physical display working area'
    }
    placements = $placements
} | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $resolvedEvidence 'window-layout.json') `
        -Encoding utf8

$placements | Format-Table title, requestedScreen, actualScreen, x, y, width, height
