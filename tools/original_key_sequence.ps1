param(
    [Parameter(Mandatory)] [string]$Sequence,
    [string]$Capture = '',
    [int]$MoveWaitMilliseconds = 215,
    [int]$MunchWaitMilliseconds = 270,
    [int]$FinalWaitMilliseconds = 300,
    [string]$PidFile = 'analysis\dosbox.pid'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
if (-not ('OriginalSequenceNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class OriginalSequenceNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rectangle);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);
    [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
}
'@
}

[void][OriginalSequenceNative]::SetThreadDpiAwarenessContext([IntPtr](-4))

$process = Get-Process -Id ([int](Get-Content -LiteralPath $PidFile))
$window = [IntPtr]$process.MainWindowHandle
if ($window -eq [IntPtr]::Zero) { throw 'DOSBox-X has no main window.' }
[OriginalSequenceNative]::SetForegroundWindow($window) | Out-Null
Start-Sleep -Milliseconds 100

$keyMap = @{
    U = 0x26
    R = 0x27
    D = 0x28
    L = 0x25
    M = 0x20
    E = 0x0D
}
foreach ($token in ($Sequence.ToUpperInvariant() -split '[,\s]+' | Where-Object { $_ })) {
    if (-not $keyMap.ContainsKey($token)) { throw "Unknown sequence token '$token'." }
    $virtualKey = [byte]$keyMap[$token]
    $scanCode = [byte][OriginalSequenceNative]::MapVirtualKeyW($virtualKey, 0)
    [OriginalSequenceNative]::keybd_event($virtualKey, $scanCode, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 25
    [OriginalSequenceNative]::keybd_event($virtualKey, $scanCode, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $(if ($token -eq 'M') { $MunchWaitMilliseconds } else { $MoveWaitMilliseconds })
}
Start-Sleep -Milliseconds $FinalWaitMilliseconds

if ($Capture) {
    $rectangle = [OriginalSequenceNative+RECT]::new()
    if (-not [OriginalSequenceNative]::GetWindowRect($window, [ref]$rectangle)) {
        throw 'GetWindowRect failed.'
    }
    $width = $rectangle.Right - $rectangle.Left
    $height = $rectangle.Bottom - $rectangle.Top
    $path = [System.IO.Path]::GetFullPath($Capture)
    [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($path)) | Out-Null
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $deviceContext = $graphics.GetHdc()
    try {
        if (-not [OriginalSequenceNative]::PrintWindow($window, $deviceContext, 2)) {
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
