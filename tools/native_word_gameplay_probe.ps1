param(
    [string]$ExePath = (Join-Path $PSScriptRoot "..\build\Number Munchers.exe"),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\analysis\native-word-gameplay"),
    [switch]$AllowVisibleWindow
)

$ErrorActionPreference = "Stop"

if (-not $AllowVisibleWindow) {
    throw "This muted probe opens and controls one native game window. Pass -AllowVisibleWindow only after explicit user authorization."
}

$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

Add-Type -AssemblyName System.Drawing.Common
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Threading;

public static class WordGameplayProbeNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr window, int command);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter,
        int x, int y, int width, int height, uint flags);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr window, out RECT rectangle);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr window, IntPtr deviceContext, uint flags);

    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr window, uint message,
        UIntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern void keybd_event(byte virtualKey, byte scanCode,
        uint flags, UIntPtr extraInfo);

    public static void Tap(IntPtr window, byte virtualKey, int settleMilliseconds) {
        PostMessageW(window, 0x0100, (UIntPtr)virtualKey, IntPtr.Zero);
        Thread.Sleep(18);
        PostMessageW(window, 0x0101, (UIntPtr)virtualKey,
            new IntPtr(unchecked((long)0xC0000000)));
        Thread.Sleep(settleMilliseconds);
    }

    public static void QuickTap(IntPtr window, byte virtualKey) {
        PostMessageW(window, 0x0100, (UIntPtr)virtualKey, IntPtr.Zero);
        Thread.Sleep(10);
        PostMessageW(window, 0x0101, (UIntPtr)virtualKey,
            new IntPtr(unchecked((long)0xC0000000)));
    }

    public static void Cheat(IntPtr window) {
        keybd_event(0x11, 0, 0, UIntPtr.Zero);
        keybd_event(0x12, 0, 0, UIntPtr.Zero);
        PostMessageW(window, 0x0104, (UIntPtr)0x70, new IntPtr(1L << 29));
        Thread.Sleep(18);
        PostMessageW(window, 0x0105, (UIntPtr)0x70,
            new IntPtr(unchecked((long)0xE0000000)));
        keybd_event(0x12, 0, 2, UIntPtr.Zero);
        keybd_event(0x11, 0, 2, UIntPtr.Zero);
        Thread.Sleep(120);
    }
}
'@

function Save-ClientScreenshot([IntPtr]$Window, [string]$Path) {
    $rectangle = [WordGameplayProbeNative+RECT]::new()
    if (-not [WordGameplayProbeNative]::GetClientRect($Window, [ref]$rectangle)) {
        throw "GetClientRect failed"
    }
    $width = $rectangle.Right - $rectangle.Left
    $height = $rectangle.Bottom - $rectangle.Top
    if ($width -le 0 -or $height -le 0) { throw "The native client is empty" }

    $bitmap = [Drawing.Bitmap]::new($width, $height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $device = $graphics.GetHdc()
        try {
            if (-not [WordGameplayProbeNative]::PrintWindow($Window, $device, 3)) {
                throw "PrintWindow failed"
            }
        }
        finally {
            $graphics.ReleaseHdc($device)
        }
        $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function File-Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

$settingsDirectory = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'NumberMunchersNative'
$settingsPath = Join-Path $settingsDirectory 'settings.txt'
$settingsExisted = Test-Path -LiteralPath $settingsPath
$settingsBytes = if ($settingsExisted) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
$process = $null
$window = [IntPtr]::Zero

try {
    New-Item -ItemType Directory -Force -Path $settingsDirectory | Out-Null
    if ($settingsExisted) {
        $settingsText = [Text.Encoding]::UTF8.GetString($settingsBytes)
        $flags = [regex]::new('(?m)^(\d+)\s+[01]\s+[01](\s+)')
        $muted = $flags.Replace($settingsText, '$1 0 0$2', 1)
        if ($muted -eq $settingsText) { throw "Could not mute the saved settings" }
    }
    else {
        $muted = @"
NUMBER_MUNCHERS_NATIVE_SETTINGS 2
0 0 0 0 0 "" ""
1 2 5 0 5 15
1 3 5 0 50 15
1 2 25 1 50 15
1 1 20 0 50 15
1 1 10 0 50 3
1 0 0 1 50 15
"@
    }
    [IO.File]::WriteAllText($settingsPath, $muted, [Text.UTF8Encoding]::new($false))

    $process = Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path $ExePath) `
        -WindowStyle Normal -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        $window = $process.MainWindowHandle
    } while ($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if ($window -eq [IntPtr]::Zero) { throw "The native game did not create a window" }

    [void][WordGameplayProbeNative]::ShowWindow($window, 9)
    [void][WordGameplayProbeNative]::SetWindowPos($window, [IntPtr](-1),
        40, 40, 978, 647, 0x0040)
    [void][WordGameplayProbeNative]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 250

    # Launcher -> Word (the launcher accepts W directly) -> version -> splash -> title.
    [WordGameplayProbeNative]::Tap($window, 0x57, 180)
    [WordGameplayProbeNative]::Tap($window, 0x0D, 250)
    $versionPath = Join-Path $OutputDirectory '00-version.png'
    Save-ClientScreenshot $window $versionPath
    [WordGameplayProbeNative]::Tap($window, 0x0D, 250)
    $splashPath = Join-Path $OutputDirectory '01-splash.png'
    Save-ClientScreenshot $window $splashPath
    [WordGameplayProbeNative]::Tap($window, 0x0D, 250)
    $titlePath = Join-Path $OutputDirectory '02-title.png'
    Save-ClientScreenshot $window $titlePath

    [WordGameplayProbeNative]::Cheat($window)
    $cheatPath = Join-Path $OutputDirectory '03-cheat-level-select.png'
    Save-ClientScreenshot $window $cheatPath
    [WordGameplayProbeNative]::Tap($window, 0x0D, 450)
    $boardPath = Join-Path $OutputDirectory '04-board.png'
    Save-ClientScreenshot $window $boardPath

    $startupHashes = @($versionPath, $splashPath, $titlePath, $cheatPath, $boardPath) |
        ForEach-Object { File-Hash $_ } | Sort-Object -Unique
    if ($startupHashes.Count -ne 5) {
        throw "Word startup, cheat selector, and board were not five distinct frames"
    }

    $directionNames = @('right', 'left', 'up', 'down')
    $directionKeys = @(0x27, 0x25, 0x26, 0x28)
    $selectedDirection = $null
    $moveMidPath = $null
    $moveEndPath = $null
    for ($index = 0; $index -lt $directionKeys.Count; $index++) {
        $baselinePath = Join-Path $OutputDirectory ("05-{0}-baseline.png" -f $directionNames[$index])
        Save-ClientScreenshot $window $baselinePath
        $baselineHash = File-Hash $baselinePath
        [WordGameplayProbeNative]::QuickTap($window, [byte]$directionKeys[$index])
        Start-Sleep -Milliseconds 75
        $candidateMid = Join-Path $OutputDirectory ("06-{0}-move.png" -f $directionNames[$index])
        Save-ClientScreenshot $window $candidateMid
        Start-Sleep -Milliseconds 220
        $candidateEnd = Join-Path $OutputDirectory ("07-{0}-arrived.png" -f $directionNames[$index])
        Save-ClientScreenshot $window $candidateEnd
        if ((File-Hash $candidateMid) -ne $baselineHash -or
            (File-Hash $candidateEnd) -ne $baselineHash) {
            $selectedDirection = $directionNames[$index]
            $moveMidPath = $candidateMid
            $moveEndPath = $candidateEnd
            break
        }
    }
    if ($null -eq $selectedDirection) { throw "No legal Word movement was visible" }

    [WordGameplayProbeNative]::QuickTap($window, 0x20)
    Start-Sleep -Milliseconds 65
    $chewAPath = Join-Path $OutputDirectory '08-chew-a.png'
    Save-ClientScreenshot $window $chewAPath
    Start-Sleep -Milliseconds 70
    $chewBPath = Join-Path $OutputDirectory '09-chew-b.png'
    Save-ClientScreenshot $window $chewBPath
    Start-Sleep -Milliseconds 300
    $chewEndPath = Join-Path $OutputDirectory '10-chew-terminal.png'
    Save-ClientScreenshot $window $chewEndPath

    $arrivedHash = File-Hash $moveEndPath
    if ((File-Hash $chewAPath) -eq $arrivedHash -and
        (File-Hash $chewBPath) -eq $arrivedHash -and
        (File-Hash $chewEndPath) -eq $arrivedHash) {
        throw "Space did not visibly enter the Word chew path"
    }

    $report = [ordered]@{
        executable = $ExePath
        audio_muted = $true
        word_startup = 'passed'
        word_cheat_level_select = 'passed'
        word_board = 'passed'
        movement_direction = $selectedDirection
        movement_visible = 'passed'
        chew_visible = 'passed'
        screenshots = @(
            $versionPath, $splashPath, $titlePath, $cheatPath, $boardPath,
            $moveMidPath, $moveEndPath, $chewAPath, $chewBPath, $chewEndPath
        )
    }
    $reportPath = Join-Path $OutputDirectory 'report.json'
    [IO.File]::WriteAllText(
        $reportPath,
        ($report | ConvertTo-Json -Depth 3) + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
    $report | ConvertTo-Json -Depth 3
}
finally {
    if ($process -and -not $process.HasExited) {
        if ($window -ne [IntPtr]::Zero) {
            [void][WordGameplayProbeNative]::PostMessageW(
                $window, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        }
        if (-not $process.WaitForExit(3000)) {
            $process.Kill()
            $process.WaitForExit()
        }
    }
    if ($settingsExisted) {
        [IO.File]::WriteAllBytes($settingsPath, $settingsBytes)
    }
    elseif (Test-Path -LiteralPath $settingsPath) {
        Remove-Item -LiteralPath $settingsPath -Force
    }
}
