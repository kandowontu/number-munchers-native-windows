param(
    [string]$Keys = '',
    [int[]]$VirtualKeys = @(),
    [int[]]$Chord = @(),
    [string]$PasteText = '',
    [int[]]$MenuCommands = @(),
    [string]$Capture = '',
    [int]$WaitMilliseconds = 800,
    [switch]$OnScreen,
    [switch]$DoNotShow,
    [int]$WindowWidth = 0,
    [int]$WindowHeight = 0,
    [string]$PidFile = 'analysis\dosbox.pid'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
if (-not ('OriginalHarnessNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class OriginalHarnessNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hwnd, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rectangle);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hwnd, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr hwnd, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);
    [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint first, uint second, bool attach);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);

    public static void ForceFocus(IntPtr hwnd) {
        uint ignored;
        uint targetThread = GetWindowThreadProcessId(hwnd, out ignored);
        uint foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), out ignored);
        uint currentThread = GetCurrentThreadId();
        if (currentThread != targetThread) AttachThreadInput(currentThread, targetThread, true);
        if (foregroundThread != targetThread) AttachThreadInput(foregroundThread, targetThread, true);
        ShowWindow(hwnd, 9);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        if (foregroundThread != targetThread) AttachThreadInput(foregroundThread, targetThread, false);
        if (currentThread != targetThread) AttachThreadInput(currentThread, targetThread, false);
    }
}
'@
}

# Work in physical pixels so a requested 960x600 DOS surface and captured
# window rectangle are not silently virtualized by the desktop DPI scale.
[void][OriginalHarnessNative]::SetThreadDpiAwarenessContext([IntPtr](-4))

$processId = [int](Get-Content -LiteralPath $PidFile)
$process = Get-Process -Id $processId
$window = [IntPtr]$process.MainWindowHandle
if ($window -eq [IntPtr]::Zero) {
    throw "DOSBox-X process $processId has no main window"
}

$rectangle = New-Object OriginalHarnessNative+RECT
[OriginalHarnessNative]::GetWindowRect($window, [ref]$rectangle) | Out-Null
$width = $rectangle.Right - $rectangle.Left
$height = $rectangle.Bottom - $rectangle.Top
if ($WindowWidth -gt 0) { $width = $WindowWidth }
if ($WindowHeight -gt 0) { $height = $WindowHeight }
if (-not $DoNotShow) {
    [OriginalHarnessNative]::ShowWindow($window, 9) | Out-Null
    Start-Sleep -Milliseconds 100
    [OriginalHarnessNative]::MoveWindow($window, $(if ($OnScreen) { 40 } else { -2000 }), 20, $width, $height, $true) | Out-Null
}

if ($Keys) {
    [OriginalHarnessNative]::SetForegroundWindow($window) | Out-Null
    [System.Windows.Forms.SendKeys]::SendWait($Keys)
}
if ($PasteText) {
    Set-Clipboard -Value $PasteText
    [OriginalHarnessNative]::PostMessageW($window, 0x0111, [UIntPtr]4105, [IntPtr]::Zero) | Out-Null
}
foreach ($command in $MenuCommands) {
    [OriginalHarnessNative]::SendMessageW($window, 0x0111, [UIntPtr]$command, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 80
}
foreach ($virtualKey in $VirtualKeys) {
    [OriginalHarnessNative]::ForceFocus($window)
    Start-Sleep -Milliseconds 100
    $scanCode = [OriginalHarnessNative]::MapVirtualKeyW([uint32]$virtualKey, 0)
    [OriginalHarnessNative]::keybd_event([byte]$virtualKey, [byte]$scanCode, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 35
    [OriginalHarnessNative]::keybd_event([byte]$virtualKey, [byte]$scanCode, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
}
if ($Chord.Count -gt 0) {
    [OriginalHarnessNative]::ForceFocus($window)
    Start-Sleep -Milliseconds 100
    foreach ($virtualKey in $Chord) {
        $scanCode = [OriginalHarnessNative]::MapVirtualKeyW([uint32]$virtualKey, 0)
        [OriginalHarnessNative]::keybd_event([byte]$virtualKey, [byte]$scanCode, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 30
    }
    for ($index = $Chord.Count - 1; $index -ge 0; --$index) {
        $virtualKey = $Chord[$index]
        $scanCode = [OriginalHarnessNative]::MapVirtualKeyW([uint32]$virtualKey, 0)
        [OriginalHarnessNative]::keybd_event([byte]$virtualKey, [byte]$scanCode, 2, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 30
    }
}
Start-Sleep -Milliseconds $WaitMilliseconds

if ($Capture) {
    $capturePath = [System.IO.Path]::GetFullPath($Capture)
    $captureDirectory = [System.IO.Path]::GetDirectoryName($capturePath)
    [System.IO.Directory]::CreateDirectory($captureDirectory) | Out-Null
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    try {
        if (-not [OriginalHarnessNative]::PrintWindow($window, $hdc, 2)) {
            throw 'PrintWindow failed'
        }
    }
    finally {
        $graphics.ReleaseHdc($hdc)
        $graphics.Dispose()
    }
    try {
        $bitmap.Save($capturePath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $bitmap.Dispose()
    }
    Write-Output $capturePath
}
