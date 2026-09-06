param(
    [string]$ExePath = (Join-Path $PSScriptRoot "..\build\Number Munchers.exe"),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\analysis\native-flicker"),
    [switch]$AllowVisibleWindow
)

$ErrorActionPreference = "Stop"

if (-not $AllowVisibleWindow) {
    throw "This probe opens and controls the native game window. Pass -AllowVisibleWindow only after explicit user authorization."
}

$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$ffmpegCommand = Get-Command ffmpeg -ErrorAction Stop
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$videoPath = Join-Path $OutputDirectory "native-presentation-probe.mkv"
$captureLogPath = Join-Path $OutputDirectory "native-presentation-probe-ffmpeg.log"

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Threading;

public static class MunchersFlickerNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

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
    public static extern bool ClientToScreen(IntPtr window, ref POINT point);

    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr window, uint message,
        UIntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern void keybd_event(byte virtualKey, byte scanCode,
        uint flags, UIntPtr extraInfo);

    public static void Tap(IntPtr window, byte virtualKey) {
        PostMessageW(window, 0x0100, (UIntPtr)virtualKey, IntPtr.Zero);
        Thread.Sleep(18);
        PostMessageW(window, 0x0101, (UIntPtr)virtualKey,
            new IntPtr(unchecked((long)0xC0000000)));
        Thread.Sleep(90);
    }

    public static void AltEnter(IntPtr window) {
        PostMessageW(window, 0x0104, (UIntPtr)0x0D, new IntPtr(1L << 29));
        Thread.Sleep(18);
        PostMessageW(window, 0x0105, (UIntPtr)0x0D,
            new IntPtr(unchecked((long)0xE0000000)));
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
        Thread.Sleep(100);
    }
}
'@

$settingsDirectory = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'NumberMunchersNative'
$settingsPath = Join-Path $settingsDirectory 'settings.txt'
$settingsExisted = Test-Path -LiteralPath $settingsPath
$settingsBytes = if ($settingsExisted) { [IO.File]::ReadAllBytes($settingsPath) } else { $null }
$process = $null
$capture = $null

try {
    New-Item -ItemType Directory -Force -Path $settingsDirectory | Out-Null
    if ($settingsExisted) {
        $settingsText = [Text.Encoding]::UTF8.GetString($settingsBytes)
        $flags = [regex]::new('(?m)^(\d+)\s+[01]\s+[01](\s+)')
        $muted = $flags.Replace($settingsText, '$1 0 0$2', 1)
        if ($muted -eq $settingsText) { throw "Could not mute the saved Number settings." }
    } else {
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
    if ($window -eq [IntPtr]::Zero) { throw "The native game did not create a window." }

    [void][MunchersFlickerNative]::ShowWindow($window, 9)
    # Pin the test window above unrelated desktop applications. The previous
    # probe deliberately rejected a recording when this was not guaranteed.
    [void][MunchersFlickerNative]::SetWindowPos($window, [IntPtr](-1),
        40, 40, 978, 647, 0x0040)
    [void][MunchersFlickerNative]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 300

    # Launcher -> Number -> version -> splash -> title -> cheat level board.
    [MunchersFlickerNative]::Tap($window, 0x0D)
    [MunchersFlickerNative]::Tap($window, 0x0D)
    [MunchersFlickerNative]::Tap($window, 0x0D)
    [MunchersFlickerNative]::Cheat($window)
    [MunchersFlickerNative]::Tap($window, 0x0D)
    Start-Sleep -Milliseconds 500

    [void][MunchersFlickerNative]::SetWindowPos($window, [IntPtr](-1),
        40, 40, 978, 647, 0x0040)
    $client = [MunchersFlickerNative+RECT]::new()
    $origin = [MunchersFlickerNative+POINT]::new()
    if (-not [MunchersFlickerNative]::GetClientRect($window, [ref]$client) -or
        -not [MunchersFlickerNative]::ClientToScreen($window, [ref]$origin)) {
        throw "Could not resolve the game client capture rectangle."
    }
    $clientWidth = $client.Right - $client.Left
    $clientHeight = $client.Bottom - $client.Top
    $captureX = $origin.X + 10
    $captureY = $origin.Y + 10
    $captureWidth = [Math]::Min(880, $clientWidth - 20)
    $captureHeight = [Math]::Min(500, $clientHeight - 20)
    if ($captureWidth -lt 640 -or $captureHeight -lt 400) {
        throw "The game client is unexpectedly small: $clientWidth x $clientHeight."
    }

    # The fixed desktop crop lies inside both the restored window and the
    # fullscreen client. It therefore observes actual DWM presentation rather
    # than PrintWindow's application-owned backing surface.
    $captureArguments = @(
        '-hide_banner', '-loglevel', 'error', '-y',
        '-f', 'gdigrab', '-framerate', '120',
        '-offset_x', "$captureX", '-offset_y', "$captureY",
        '-video_size', ("{0}x{1}" -f $captureWidth, $captureHeight), '-i', 'desktop',
        '-t', '7', '-c:v', 'ffv1', '-level', '3', ('"' + $videoPath + '"')
    )
    $capture = Start-Process -FilePath $ffmpegCommand.Source -ArgumentList $captureArguments `
        -RedirectStandardError $captureLogPath -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 700

    [MunchersFlickerNative]::Tap($window, 0x27)
    [MunchersFlickerNative]::Tap($window, 0x28)
    Start-Sleep -Milliseconds 500
    [MunchersFlickerNative]::AltEnter($window)
    Start-Sleep -Milliseconds 700
    [MunchersFlickerNative]::AltEnter($window)
    Start-Sleep -Milliseconds 450
    [void][MunchersFlickerNative]::SetWindowPos($window, [IntPtr](-1),
        40, 40, 978, 647, 0x0040)

    [void][MunchersFlickerNative]::SetWindowPos($window, [IntPtr](-1),
        40, 40, 1120, 760, 0x0040)
    Start-Sleep -Milliseconds 350
    [void][MunchersFlickerNative]::SetWindowPos($window, [IntPtr](-1),
        40, 40, 978, 647, 0x0040)
    Start-Sleep -Milliseconds 350

    [MunchersFlickerNative]::AltEnter($window)
    Start-Sleep -Milliseconds 500
    [MunchersFlickerNative]::AltEnter($window)
    Start-Sleep -Milliseconds 900
    [MunchersFlickerNative]::Tap($window, 0x28)

    if (-not $capture.WaitForExit(12000)) {
        $capture.Kill()
        $capture.WaitForExit()
        throw "Desktop capture did not terminate."
    }
    if ($capture.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $videoPath)) {
        $captureLog = if (Test-Path -LiteralPath $captureLogPath) {
            Get-Content -LiteralPath $captureLogPath -Raw
        } else { "no ffmpeg log" }
        throw "Desktop capture failed with exit code $($capture.ExitCode): $captureLog"
    }

    # Exclude a generous outer margin so window chrome or an unrelated desktop
    # pixel cannot conceal a black client presentation from the YMAX gate.
    $analysisX = 40
    $analysisY = 40
    $analysisWidth = $captureWidth - 80
    $analysisHeight = $captureHeight - 80
    $analysisFilter = "crop=$analysisWidth`:$analysisHeight`:$analysisX`:$analysisY," +
        'signalstats,metadata=print:key=lavfi.signalstats.YAVG,' +
        'metadata=print:key=lavfi.signalstats.YMAX'
    $metadata = & $ffmpegCommand.Source -hide_banner -loglevel info -i $videoPath `
        -vf $analysisFilter `
        -f null NUL 2>&1
    $averages = @($metadata | ForEach-Object {
        if ($_ -match 'lavfi\.signalstats\.YAVG=([0-9.]+)') { [double]$Matches[1] }
    })
    $maximums = @($metadata | ForEach-Object {
        if ($_ -match 'lavfi\.signalstats\.YMAX=([0-9.]+)') { [double]$Matches[1] }
    })
    if ($averages.Count -eq 0 -or $averages.Count -ne $maximums.Count) {
        throw "Could not read per-frame presentation statistics."
    }
    $nearBlack = @($maximums | Where-Object { $_ -le 24.0 }).Count
    $minimumAverage = ($averages | Measure-Object -Minimum).Minimum
    $minimumMaximum = ($maximums | Measure-Object -Minimum).Minimum

    $sampleTimes = @(1.0, 2.1, 3.0, 3.7, 4.5)
    $samplePaths = @()
    for ($index = 0; $index -lt $sampleTimes.Count; ++$index) {
        $samplePath = Join-Path $OutputDirectory ('sample-{0:d2}.png' -f $index)
        & $ffmpegCommand.Source -hide_banner -loglevel error -y `
            -ss $sampleTimes[$index] -i $videoPath -frames:v 1 $samplePath
        if ($LASTEXITCODE -ne 0) { throw "Could not extract sample frame $index." }
        $samplePaths += $samplePath
    }

    $result = [pscustomobject]@{
        executable = $ExePath
        capture = $videoPath
        capture_region = @($captureX, $captureY, $captureWidth, $captureHeight)
        analyzed_inner_region = @($analysisX, $analysisY, $analysisWidth, $analysisHeight)
        requested_fps = 120
        observed_frames = $averages.Count
        minimum_y_average = $minimumAverage
        minimum_y_maximum = $minimumMaximum
        near_black_frames_ymax_le_24 = $nearBlack
        samples = $samplePaths
        sound_and_music_forced_off = $true
    }
    $json = $result | ConvertTo-Json -Depth 3
    [IO.File]::WriteAllText(
        (Join-Path $OutputDirectory 'report.json'), $json + [Environment]::NewLine,
        [Text.UTF8Encoding]::new($false))
    $json
    if ($nearBlack -ne 0) {
        throw "The visible capture contains $nearBlack near-black presentation frame(s)."
    }
}
finally {
    if ($capture -and -not $capture.HasExited) {
        $capture.Kill()
        $capture.WaitForExit()
    }
    if ($process -and -not $process.HasExited) {
        [void][MunchersFlickerNative]::PostMessageW(
            $process.MainWindowHandle, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        if (-not $process.WaitForExit(3000)) {
            $process.Kill()
            $process.WaitForExit()
        }
    }
    if ($settingsExisted) {
        [IO.File]::WriteAllBytes($settingsPath, $settingsBytes)
    } elseif (Test-Path -LiteralPath $settingsPath) {
        Remove-Item -LiteralPath $settingsPath -Force
    }
}
