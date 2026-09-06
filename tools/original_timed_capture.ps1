param(
    [AllowEmptyString()] [string]$PasteText = '',
    [Parameter(Mandatory)] [string]$OutputPrefix,
    [int[]]$IntervalsMilliseconds = @(0, 60, 60, 60, 60, 60, 60),
    [string]$PidFile = 'analysis\dosbox.pid'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing.Common
if (-not ('OriginalTimedCaptureNative' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OriginalTimedCaptureNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr deviceContext, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
}
'@
}

[void][OriginalTimedCaptureNative]::SetThreadDpiAwarenessContext([IntPtr](-4))

$process = Get-Process -Id ([int](Get-Content -LiteralPath $PidFile))
$window = [IntPtr]$process.MainWindowHandle
$rectangle = [OriginalTimedCaptureNative+RECT]::new()
if (-not [OriginalTimedCaptureNative]::GetWindowRect($window, [ref]$rectangle)) {
    throw 'GetWindowRect failed'
}
$width = $rectangle.Right - $rectangle.Left
$height = $rectangle.Bottom - $rectangle.Top
$prefix = [System.IO.Path]::GetFullPath($OutputPrefix)
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($prefix)) | Out-Null

if ($PasteText.Length -gt 0) {
    Set-Clipboard -Value $PasteText
    [OriginalTimedCaptureNative]::PostMessageW($window, 0x0111, [UIntPtr]4105, [IntPtr]::Zero) | Out-Null
}

$elapsed = 0
for ($index = 0; $index -lt $IntervalsMilliseconds.Count; $index++) {
    $delay = $IntervalsMilliseconds[$index]
    if ($delay -gt 0) { Start-Sleep -Milliseconds $delay }
    $elapsed += $delay
    $path = '{0}-{1:D2}-{2:D4}ms.png' -f $prefix, $index, $elapsed
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $deviceContext = $graphics.GetHdc()
    try {
        if (-not [OriginalTimedCaptureNative]::PrintWindow($window, $deviceContext, 2)) {
            throw 'PrintWindow failed'
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
