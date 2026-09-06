param(
    [string]$Sequence = '',
    [Parameter(Mandatory)] [string]$OutputPrefix,
    [int]$Count = 60,
    [int]$IntervalMilliseconds = 20,
    [int]$InitialKeyDelayMilliseconds = 100,
    [int]$KeyIntervalMilliseconds = 350,
    [string]$PidFile = 'analysis\dosbox.pid'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing.Common
if (-not ('OriginalKeyCaptureSeriesNative' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OriginalKeyCaptureSeriesNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr deviceContext, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint first, uint second, bool attach);
    [DllImport("user32.dll")] public static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);
    [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);

    public static void ForceFocus(IntPtr window) {
        uint ignored;
        uint targetThread = GetWindowThreadProcessId(window, out ignored);
        uint foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), out ignored);
        uint currentThread = GetCurrentThreadId();
        if (currentThread != targetThread) AttachThreadInput(currentThread, targetThread, true);
        if (foregroundThread != targetThread) AttachThreadInput(foregroundThread, targetThread, true);
        ShowWindow(window, 9);
        BringWindowToTop(window);
        SetForegroundWindow(window);
        SetFocus(window);
        if (foregroundThread != targetThread) AttachThreadInput(foregroundThread, targetThread, false);
        if (currentThread != targetThread) AttachThreadInput(currentThread, targetThread, false);
    }
}
'@
}

[void][OriginalKeyCaptureSeriesNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
$process = Get-Process -Id ([int](Get-Content -LiteralPath $PidFile))
$window = [IntPtr]$process.MainWindowHandle
if ($window -eq [IntPtr]::Zero) { throw 'DOSBox-X has no main window.' }
$rectangle = [OriginalKeyCaptureSeriesNative+RECT]::new()
if (-not [OriginalKeyCaptureSeriesNative]::GetWindowRect($window, [ref]$rectangle)) {
    throw 'GetWindowRect failed.'
}
$width = $rectangle.Right - $rectangle.Left
$height = $rectangle.Bottom - $rectangle.Top
$prefix = [System.IO.Path]::GetFullPath($OutputPrefix)
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($prefix)) | Out-Null

$keyMap = @{
    U = 0x26
    R = 0x27
    D = 0x28
    L = 0x25
    M = 0x20
    E = 0x0D
}
[OriginalKeyCaptureSeriesNative]::ForceFocus($window)
Start-Sleep -Milliseconds 100
$tokens = @($Sequence.ToUpperInvariant() -split '[,\s]+' | Where-Object { $_ })
foreach ($token in $tokens) {
    if (-not $keyMap.ContainsKey($token)) { throw "Unknown sequence token '$token'." }
}

$watch = [System.Diagnostics.Stopwatch]::StartNew()
$keyIndex = 0
for ($index = 0; $index -lt $Count; ++$index) {
    $targetMilliseconds = $index * $IntervalMilliseconds
    $remaining = $targetMilliseconds - [int]$watch.ElapsedMilliseconds
    if ($remaining -gt 0) { Start-Sleep -Milliseconds $remaining }
    if ($keyIndex -lt $tokens.Count -and
        $watch.ElapsedMilliseconds -ge
            $InitialKeyDelayMilliseconds + $keyIndex * $KeyIntervalMilliseconds) {
        $token = $tokens[$keyIndex]
        $virtualKey = [byte]$keyMap[$token]
        $scanCode = [byte][OriginalKeyCaptureSeriesNative]::MapVirtualKeyW($virtualKey, 0)
        [OriginalKeyCaptureSeriesNative]::keybd_event(
            $virtualKey, $scanCode, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 25
        [OriginalKeyCaptureSeriesNative]::keybd_event(
            $virtualKey, $scanCode, 2, [UIntPtr]::Zero)
        ++$keyIndex
    }
    $path = '{0}-{1:D3}-{2:D5}ms.png' -f $prefix, $index, [int]$watch.ElapsedMilliseconds
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $deviceContext = $graphics.GetHdc()
    try {
        if (-not [OriginalKeyCaptureSeriesNative]::PrintWindow($window, $deviceContext, 2)) {
            throw 'PrintWindow failed.'
        }
    }
    finally {
        $graphics.ReleaseHdc($deviceContext)
        $graphics.Dispose()
    }
    try { $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
    $path
}
