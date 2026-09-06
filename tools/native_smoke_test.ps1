param(
    [string]$ExePath = (Join-Path $PSScriptRoot "..\build\Number Munchers.exe"),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\analysis\native-smoke"),
    [switch]$AllowVisibleWindow
)

$ErrorActionPreference = "Stop"

if (-not $AllowVisibleWindow) {
    throw "Visible native smoke testing is blocked by default because it opens and focuses the game window. Re-run with -AllowVisibleWindow only when the user has explicitly authorized a visible test."
}

$ExePath = (Resolve-Path $ExePath).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path

Add-Type -AssemblyName System.Drawing.Common
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class NumberMunchersSmokeNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr window, int command);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter, int x, int y,
                                           int width, int height, uint flags);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr window, out RECT rectangle);

    [DllImport("user32.dll")]
    public static extern bool ClientToScreen(IntPtr window, ref POINT point);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr window, IntPtr deviceContext, uint flags);

    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")]
    public static extern IntPtr GetWindowLongPtr(IntPtr window, int index);

    [DllImport("user32.dll")]
    public static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);

    [DllImport("kernel32.dll")]
    public static extern void Sleep(uint milliseconds);

    public static void Tap(IntPtr window, byte virtualKey) {
        PostMessageW(window, 0x0100, (UIntPtr)virtualKey, IntPtr.Zero);
        Sleep(35);
        PostMessageW(window, 0x0101, (UIntPtr)virtualKey, new IntPtr(unchecked((long)0xC0000000)));
        Sleep(150);
    }

    public static void QuickTap(IntPtr window, byte virtualKey) {
        PostMessageW(window, 0x0100, (UIntPtr)virtualKey, IntPtr.Zero);
        Sleep(15);
        PostMessageW(window, 0x0101, (UIntPtr)virtualKey, new IntPtr(unchecked((long)0xC0000000)));
    }

    public static void Chord(IntPtr window, byte modifier1, byte modifier2, byte key) {
        keybd_event(modifier1, 0, 0, UIntPtr.Zero);
        keybd_event(modifier2, 0, 0, UIntPtr.Zero);
        PostMessageW(window, 0x0104, (UIntPtr)key, (IntPtr)(1L << 29));
        Sleep(45);
        PostMessageW(window, 0x0105, (UIntPtr)key, new IntPtr(unchecked((long)0xE0000000)));
        keybd_event(modifier2, 0, 2, UIntPtr.Zero);
        keybd_event(modifier1, 0, 2, UIntPtr.Zero);
        Sleep(100);
    }

    public static void AltEnter(IntPtr window) {
        keybd_event(0x12, 0, 0, UIntPtr.Zero);
        PostMessageW(window, 0x0104, (UIntPtr)0x0D, (IntPtr)(1L << 29));
        Sleep(45);
        PostMessageW(window, 0x0105, (UIntPtr)0x0D, new IntPtr(unchecked((long)0xE0000000)));
        keybd_event(0x12, 0, 2, UIntPtr.Zero);
        Sleep(100);
    }

    public static void MovePointer(IntPtr window, int x, int y) {
        long packed = ((long)(y & 0xffff) << 16) | (long)(x & 0xffff);
        PostMessageW(window, 0x0200, UIntPtr.Zero, new IntPtr(packed));
        Sleep(80);
    }

    public static int[] WindowBounds(IntPtr window) {
        RECT rectangle;
        if (!GetWindowRect(window, out rectangle)) throw new InvalidOperationException("GetWindowRect failed");
        return new [] { rectangle.Left, rectangle.Top, rectangle.Right, rectangle.Bottom };
    }

    public static long WindowStyle(IntPtr window) {
        return GetWindowLongPtr(window, -16).ToInt64();
    }

}
'@

function Save-ClientScreenshot([IntPtr]$Window, [string]$Path) {
    $rectangle = [NumberMunchersSmokeNative+RECT]::new()
    if (-not [NumberMunchersSmokeNative]::GetClientRect($Window, [ref]$rectangle)) {
        throw "GetClientRect failed"
    }
    $width = $rectangle.Right - $rectangle.Left
    $height = $rectangle.Bottom - $rectangle.Top
    $bitmap = [System.Drawing.Bitmap]::new($width, $height,
        [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $deviceContext = $graphics.GetHdc()
        try {
            if (-not [NumberMunchersSmokeNative]::PrintWindow($Window, $deviceContext, 3)) {
                throw "PrintWindow failed"
            }
        }
        finally {
            $graphics.ReleaseHdc($deviceContext)
        }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

# Disable DPI virtualization for the harness so dimensions match the DPI-aware game.
[void][NumberMunchersSmokeNative]::SetThreadDpiAwarenessContext([IntPtr](-4))

$process = Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path $ExePath) -WindowStyle Normal -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        $window = $process.MainWindowHandle
    } while ($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if ($window -eq [IntPtr]::Zero) { throw "The game did not create a main window." }

    [void][NumberMunchersSmokeNative]::ShowWindow($window, 9)
    [void][NumberMunchersSmokeNative]::SetWindowPos($window, [IntPtr]::Zero, 40, 40, 0, 0, 0x0015)
    [void][NumberMunchersSmokeNative]::SetCursorPos(10, 10)
    [void][NumberMunchersSmokeNative]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 500

    $initialBounds = [NumberMunchersSmokeNative]::WindowBounds($window)
    $versionPath = Join-Path $OutputDirectory "00-version.png"
    Save-ClientScreenshot $window $versionPath
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    Start-Sleep -Milliseconds 250
    $splashPath = Join-Path $OutputDirectory "00-title-splash.png"
    Save-ClientScreenshot $window $splashPath
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    Start-Sleep -Milliseconds 250
    $titlePath = Join-Path $OutputDirectory "01-title.png"
    Save-ClientScreenshot $window $titlePath
    $startupHashes = @($versionPath, $splashPath, $titlePath) |
        ForEach-Object { (Get-FileHash $_).Hash } | Sort-Object -Unique
    if ($startupHashes.Count -ne 3) {
        throw "The two startup key gates did not produce distinct version, splash, and menu screens."
    }

    # Exercise the transactional Set Content editor through the packaged EXE:
    # Title -> Options -> Set Content -> Range/Other/Operations -> commit.
    for ($down = 0; $down -lt 3; $down++) {
        [NumberMunchersSmokeNative]::Tap($window, 0x28)
    }
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    [NumberMunchersSmokeNative]::Tap($window, 0x28)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    $contentBasePath = Join-Path $OutputDirectory "01-content-base.png"
    Save-ClientScreenshot $window $contentBasePath

    [NumberMunchersSmokeNative]::Tap($window, 0x70)
    $contentHelp1Path = Join-Path $OutputDirectory "01-content-help-1.png"
    Save-ClientScreenshot $window $contentHelp1Path
    [NumberMunchersSmokeNative]::Tap($window, 0x20)
    $contentHelp2Path = Join-Path $OutputDirectory "01-content-help-2.png"
    Save-ClientScreenshot $window $contentHelp2Path
    [NumberMunchersSmokeNative]::Tap($window, 0x20)

    [NumberMunchersSmokeNative]::Tap($window, 0x20) # Multiples Use: no
    [NumberMunchersSmokeNative]::Tap($window, 0x27)
    [NumberMunchersSmokeNative]::Tap($window, 0x20)
    $contentRangePath = Join-Path $OutputDirectory "01-content-range.png"
    Save-ClientScreenshot $window $contentRangePath
    [NumberMunchersSmokeNative]::Tap($window, 0x33)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    [NumberMunchersSmokeNative]::Tap($window, 0x31)
    [NumberMunchersSmokeNative]::Tap($window, 0x35)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    [NumberMunchersSmokeNative]::Tap($window, 0x27)
    [NumberMunchersSmokeNative]::Tap($window, 0x20) # in order
    [NumberMunchersSmokeNative]::Tap($window, 0x27)
    [NumberMunchersSmokeNative]::Tap($window, 0x20)
    $contentOtherPath = Join-Path $OutputDirectory "01-content-other.png"
    Save-ClientScreenshot $window $contentOtherPath
    [NumberMunchersSmokeNative]::Tap($window, 0x33)
    [NumberMunchersSmokeNative]::Tap($window, 0x30)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    [NumberMunchersSmokeNative]::Tap($window, 0x28) # Equality Other
    [NumberMunchersSmokeNative]::Tap($window, 0x20)
    $contentOperationsPath = Join-Path $OutputDirectory "01-content-operations.png"
    Save-ClientScreenshot $window $contentOperationsPath
    for ($operation = 1; $operation -lt 4; $operation++) {
        [NumberMunchersSmokeNative]::Tap($window, 0x28)
        [NumberMunchersSmokeNative]::Tap($window, 0x20)
    }
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    # Re-enable Multiples after validating the commit so the long-standing
    # six-mode smoke sequence below still exercises every selector entry.
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    [NumberMunchersSmokeNative]::Tap($window, 0x20)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    $contentHashes = @($contentBasePath, $contentHelp1Path, $contentHelp2Path,
                       $contentRangePath, $contentOtherPath, $contentOperationsPath) |
        ForEach-Object { (Get-FileHash $_).Hash } | Sort-Object -Unique
    if ($contentHashes.Count -ne 6) {
        throw "Set Content did not expose distinct base, help, range, multiplier, and operation pages."
    }

    # The original Hall eraser is transactional. Exercise the individual
    # list editor with an Escape rollback, then decline Erase ALL at its
    # default No selection so this smoke test never mutates user score data.
    [NumberMunchersSmokeNative]::Tap($window, 0x28)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    $eraseEntriesPath = Join-Path $OutputDirectory "01-erase-entries.png"
    Save-ClientScreenshot $window $eraseEntriesPath
    [NumberMunchersSmokeNative]::Tap($window, 0x20)
    [NumberMunchersSmokeNative]::Tap($window, 0x1B) # Roll back the draft.
    for ($eraseList = 0; $eraseList -lt 6; $eraseList++) {
        [NumberMunchersSmokeNative]::Tap($window, 0x28)
    }
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    $eraseAllPath = Join-Path $OutputDirectory "01-erase-all-confirm.png"
    Save-ClientScreenshot $window $eraseAllPath
    if ((Get-FileHash $eraseEntriesPath).Hash -eq (Get-FileHash $eraseAllPath).Hash) {
        throw "Individual and erase-all Hall editor pages were not distinct."
    }
    [NumberMunchersSmokeNative]::Tap($window, 0x0D) # Default No.
    [NumberMunchersSmokeNative]::Tap($window, 0x1B) # Options.
    [NumberMunchersSmokeNative]::Tap($window, 0x1B) # Title.

    # Requested Ctrl+Alt+F1 cheat/level-select chord.
    [NumberMunchersSmokeNative]::Chord($window, 0x11, 0x12, 0x70)
    Start-Sleep -Milliseconds 250
    $cheatPath = Join-Path $OutputDirectory "02-cheat-menu.png"
    Save-ClientScreenshot $window $cheatPath
    if ((Get-FileHash $titlePath).Hash -eq (Get-FileHash $cheatPath).Hash) {
        throw "Ctrl+Alt+F1 did not visibly open the cheat menu."
    }
    for ($levelStep = 0; $levelStep -lt 5; $levelStep++) {
        [NumberMunchersSmokeNative]::Tap($window, 0x27)
    }
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    Start-Sleep -Milliseconds 250
    $cheatLevelPath = Join-Path $OutputDirectory "02-level-6-board.png"
    Save-ClientScreenshot $window $cheatLevelPath
    if ((Get-FileHash $cheatPath).Hash -eq (Get-FileHash $cheatLevelPath).Hash) {
        throw "The cheat level selector did not start its selected board."
    }
    # Fresh reference boards deliberately leave the Muncher's starting cell
    # blank. Move in the first available direction before sampling the chew.
    $munchStartPath = Join-Path $OutputDirectory "02-munch-start.png"
    $moveBaselineHash = (Get-FileHash $cheatLevelPath).Hash
    $movedToPopulatedCell = $false
    foreach ($directionKey in @(0x26, 0x27, 0x28, 0x25)) {
        [NumberMunchersSmokeNative]::Tap($window, $directionKey)
        Start-Sleep -Milliseconds 250
        Save-ClientScreenshot $window $munchStartPath
        $movedHash = (Get-FileHash $munchStartPath).Hash
        if ($movedHash -ne $moveBaselineHash) {
            $movedToPopulatedCell = $true
            break
        }
    }
    if (-not $movedToPopulatedCell) {
        throw "The Muncher could not leave its blank starting cell."
    }

    # Sample munching on the populated destination. This prevents an idle or
    # pause sprite change from masquerading as a chew frame.
    [NumberMunchersSmokeNative]::QuickTap($window, 0x20)
    Start-Sleep -Milliseconds 80
    $munchPath = Join-Path $OutputDirectory "03-munch-animation.png"
    Save-ClientScreenshot $window $munchPath
    if ((Get-FileHash $munchStartPath).Hash -eq (Get-FileHash $munchPath).Hash) {
        throw "Munching did not produce a visible animation frame on a live board."
    }
    Start-Sleep -Milliseconds 500
    [NumberMunchersSmokeNative]::Tap($window, 0x20)
    [NumberMunchersSmokeNative]::Tap($window, 0x1B)
    Start-Sleep -Milliseconds 200
    $quitConfirmPath = Join-Path $OutputDirectory "03-quit-confirm.png"
    Save-ClientScreenshot $window $quitConfirmPath
    if ((Get-FileHash $munchPath).Hash -eq (Get-FileHash $quitConfirmPath).Hash) {
        throw "Escape did not visibly open the in-game quit confirmation."
    }
    [NumberMunchersSmokeNative]::Tap($window, 0x0D) # Default No resumes the board.

    # Restart cleanly after the quit-dialog check. The prior munch can either
    # score or lose a life, so a confirmed Yes can legitimately branch to
    # name entry or directly to the Hall; restarting makes later mode labels
    # deterministic without weakening the dialog check.
    [void][NumberMunchersSmokeNative]::PostMessageW($window, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(3000)) { $process.Kill(); $process.WaitForExit() }
    $process = Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path $ExePath) -WindowStyle Normal -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        $window = $process.MainWindowHandle
    } while ($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if ($window -eq [IntPtr]::Zero) { throw "The restarted game did not create a main window." }
    [void][NumberMunchersSmokeNative]::ShowWindow($window, 9)
    [void][NumberMunchersSmokeNative]::SetWindowPos($window, [IntPtr]::Zero, 40, 40, 0, 0, 0x0015)
    [void][NumberMunchersSmokeNative]::SetForegroundWindow($window)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)

    # Requested Alt+Enter fullscreen toggle, including restoration.
    $windowedStyle = [NumberMunchersSmokeNative]::WindowStyle($window)
    [NumberMunchersSmokeNative]::AltEnter($window)
    Start-Sleep -Milliseconds 350
    $fullscreenBounds = [NumberMunchersSmokeNative]::WindowBounds($window)
    $fullscreenStyle = [NumberMunchersSmokeNative]::WindowStyle($window)
    if (($fullscreenStyle -band 0x00CF0000L) -ne 0) {
        throw "Alt+Enter did not remove the overlapped-window style."
    }
    [NumberMunchersSmokeNative]::AltEnter($window)
    Start-Sleep -Milliseconds 350
    $restoredBounds = [NumberMunchersSmokeNative]::WindowBounds($window)
    $restoredStyle = [NumberMunchersSmokeNative]::WindowStyle($window)
    if (($restoredStyle -band 0x00CF0000L) -ne ($windowedStyle -band 0x00CF0000L)) {
        throw "The second Alt+Enter did not restore the window style."
    }

    # Title -> skip instructions -> Multiples, then pause and resume.
    # Explicitly point at the first title entry so the physical desktop cursor
    # cannot leave a stale hover selection in the game under test.
    [NumberMunchersSmokeNative]::MovePointer($window, 300, 294)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    Start-Sleep -Milliseconds 700
    $boardPath = Join-Path $OutputDirectory "03-multiples-board.png"
    Save-ClientScreenshot $window $boardPath
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    Start-Sleep -Milliseconds 150
    $timeOutPath = Join-Path $OutputDirectory "03-time-out.png"
    Save-ClientScreenshot $window $timeOutPath
    if ((Get-FileHash $boardPath).Hash -eq (Get-FileHash $timeOutPath).Hash) {
        throw "Enter did not visibly open the original-style Time out marker."
    }
    [NumberMunchersSmokeNative]::Tap($window, 0x0D)
    if ($process.HasExited) { throw "The game exited during navigation smoke testing." }

    $modeScreenshots = @($boardPath)
    $modeNames = @("Factors", "Primes", "Equality", "Inequality", "Challenge")
    for ($modeIndex = 1; $modeIndex -lt 6; $modeIndex++) {
        [NumberMunchersSmokeNative]::Tap($window, 0x1B)
        [NumberMunchersSmokeNative]::Tap($window, 0x25)
        [NumberMunchersSmokeNative]::Tap($window, 0x0D)
        [NumberMunchersSmokeNative]::Tap($window, 0x0D)
        [NumberMunchersSmokeNative]::Tap($window, 0x1B)
        [NumberMunchersSmokeNative]::Tap($window, 0x0D)
        [NumberMunchersSmokeNative]::Tap($window, 0x0D)
        for ($down = 0; $down -lt $modeIndex; $down++) {
            [NumberMunchersSmokeNative]::Tap($window, 0x28)
        }
        [NumberMunchersSmokeNative]::Tap($window, 0x0D)
        Start-Sleep -Milliseconds 250
        if ($process.HasExited) { throw "The game exited while starting $($modeNames[$modeIndex - 1])." }
        $modePath = Join-Path $OutputDirectory ("0{0}-{1}-board.png" -f
            ($modeIndex + 3), $modeNames[$modeIndex - 1].ToLowerInvariant())
        Save-ClientScreenshot $window $modePath
        $modeScreenshots += $modePath
    }

    if (($modeScreenshots | ForEach-Object { (Get-FileHash $_).Hash } | Sort-Object -Unique).Count -ne 6) {
        throw "The six game modes did not produce six distinct board screens."
    }

    [pscustomobject]@{
        executable = $ExePath
        process_alive = -not $process.HasExited
        startup_sequence = "passed"
        cheat_hotkey = "passed"
        cheat_level_select = "passed"
        fullscreen_hotkey = "passed"
        navigation = "passed"
        munch_animation = "passed"
        time_out = "passed"
        quit_confirmation = "passed"
        content_editor = "passed"
        hall_erase_editor = "passed"
        game_modes = "6/6 passed"
        initial_bounds = $initialBounds
        fullscreen_bounds = $fullscreenBounds
        restored_bounds = $restoredBounds
        screenshots = @($versionPath, $splashPath, $titlePath,
                        $contentBasePath, $contentHelp1Path, $contentHelp2Path,
                        $contentRangePath, $contentOtherPath, $contentOperationsPath,
                        $eraseEntriesPath, $eraseAllPath,
                        $cheatPath, $cheatLevelPath, $munchPath, $quitConfirmPath,
                        $timeOutPath) +
                      $modeScreenshots
    } | ConvertTo-Json -Depth 3
}
finally {
    if (-not $process.HasExited) {
        [void][NumberMunchersSmokeNative]::PostMessageW($window, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        if (-not $process.WaitForExit(3000)) { $process.Kill() }
    }
}
