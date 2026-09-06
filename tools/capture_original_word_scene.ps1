param(
    [Parameter(Mandatory)]
    [ValidateRange(0, 5)]
    [int]$SceneIndex,
    [double]$RunSeconds = 14.0,
    [switch]$AllowVisibleWindow
)

$ErrorActionPreference = 'Stop'
if (-not $AllowVisibleWindow) {
    throw 'Original scene capture requires explicit -AllowVisibleWindow approval.'
}

$workspace = [System.IO.Path]::GetFullPath(
    (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)))
$captureDirectory = Join-Path $workspace 'analysis\captures'
$outputDirectory = Join-Path $workspace 'analysis\word-live\scenes\original'
$variantDirectory = Join-Path $workspace (
    'analysis\word-live\scenes\reference-control\scene-{0}' -f $SceneIndex)
$fullOutput = Join-Path $outputDirectory ('scene-{0}-full.avi' -f $SceneIndex)
$switchOutput = Join-Path $outputDirectory ('scene-{0}-mode-switch.avi' -f $SceneIndex)
$configPath = Join-Path $workspace 'analysis\dosbox-x.conf'
$pidPath = Join-Path $workspace 'analysis\dosbox.pid'

foreach ($path in @($captureDirectory, $outputDirectory, $variantDirectory)) {
    $resolved = [System.IO.Path]::GetFullPath($path)
    if (-not $resolved.StartsWith($workspace, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Capture path escaped the workspace: $resolved"
    }
}
if (Test-Path -LiteralPath $fullOutput -PathType Leaf) {
    throw "Scene output already exists: $fullOutput"
}
if (-not (Select-String -LiteralPath $configPath -Pattern '^nosound=true$' -Quiet)) {
    throw 'Muted capture invariant failed: analysis/dosbox-x.conf lacks nosound=true.'
}
if (-not (Test-Path -LiteralPath (Join-Path $variantDirectory 'WM.EXE') -PathType Leaf)) {
    throw "Missing scene-control variant: $variantDirectory"
}

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class WordSceneCaptureNative {
    public delegate bool WindowCallback(IntPtr window, IntPtr value);
    [DllImport("user32.dll")]
    public static extern bool EnumWindows(WindowCallback callback, IntPtr value);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetWindowText(IntPtr window, StringBuilder text, int length);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr window, StringBuilder text, int length);
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);
}
'@

function Get-SceneWindows([uint32]$ProcessId) {
    $result = [ordered]@{ Game = [IntPtr]::Zero; Debugger = [IntPtr]::Zero }
    $callback = [WordSceneCaptureNative+WindowCallback] {
        param([IntPtr]$window, [IntPtr]$value)
        [uint32]$foundProcessId = 0
        [void][WordSceneCaptureNative]::GetWindowThreadProcessId(
            $window, [ref]$foundProcessId)
        if ($foundProcessId -eq $ProcessId) {
            $title = [Text.StringBuilder]::new(256)
            $className = [Text.StringBuilder]::new(256)
            [void][WordSceneCaptureNative]::GetWindowText($window, $title, 256)
            [void][WordSceneCaptureNative]::GetClassName($window, $className, 256)
            if ($className.ToString() -eq 'SDL_app') {
                $result.Game = $window
            } elseif ($title.ToString() -eq 'DOSBox-X Debugger') {
                $result.Debugger = $window
            }
        }
        return $true
    }
    [void][WordSceneCaptureNative]::EnumWindows($callback, [IntPtr]::Zero)
    return $result
}

$before = @{}
Get-ChildItem -LiteralPath $captureDirectory -Filter 'wm_*.avi' -File |
    ForEach-Object { $before[$_.FullName] = $true }
$processId = $null
try {
    & (Join-Path $workspace 'tools\launch_original.ps1') `
        -Game Word `
        -MountDirectory $variantDirectory `
        -Sequence 'space space enter' `
        -InitialWaitSeconds 1 `
        -PaceSeconds 2 `
        -StartupWaitSeconds 7 `
        -CaptureVideo `
        -KeepOnscreen `
        -AllowVisibleWindow | Out-Host

    $processId = [int](Get-Content -LiteralPath $pidPath)
    $process = Get-Process -Id $processId
    $windows = Get-SceneWindows ([uint32]$processId)
    if ($windows.Game -eq [IntPtr]::Zero) {
        throw 'Could not find the DOSBox-X SDL game window.'
    }

    # The startup autotype reaches the default-No instruction question. Pause
    # inside DOSBox-X, place one Enter word in the standard BIOS keyboard
    # buffer, and resume. This is guest-side input; no host raw-input injection
    # or scene-code modification is involved.
    [void][WordSceneCaptureNative]::PostMessageW(
        $windows.Game, 0x0111, [UIntPtr]5255, [IntPtr]::Zero)
    for ($attempt = 0; $attempt -lt 40; ++$attempt) {
        Start-Sleep -Milliseconds 100
        $windows = Get-SceneWindows ([uint32]$processId)
        if ($windows.Debugger -ne [IntPtr]::Zero) { break }
    }
    if ($windows.Debugger -eq [IntPtr]::Zero) {
        throw 'Could not find the DOSBox-X debugger console.'
    }
    [void][WordSceneCaptureNative]::ShowWindow($windows.Debugger, 9)
    [void][WordSceneCaptureNative]::SetForegroundWindow($windows.Debugger)
    $shell = New-Object -ComObject WScript.Shell
    Start-Sleep -Milliseconds 300
    $shell.SendKeys('sm 0040:001a 1e 00 20 00 0d 1c{ENTER}')
    Start-Sleep -Milliseconds 500
    $shell.SendKeys('{F5}')
    Start-Sleep -Seconds 1
    [void][WordSceneCaptureNative]::ShowWindow($windows.Debugger, 0)
    [void][WordSceneCaptureNative]::ShowWindow($windows.Game, 9)

    Start-Sleep -Milliseconds ([int][Math]::Round($RunSeconds * 1000.0))

    # Stop and finalize ZMBV before closing the debugger-bearing process.
    [void][WordSceneCaptureNative]::PostMessageW(
        $windows.Game, 0x0111, [UIntPtr]5217, [IntPtr]::Zero)
    Start-Sleep -Seconds 2

    $newCaptures = @(
        Get-ChildItem -LiteralPath $captureDirectory -Filter 'wm_*.avi' -File |
            Where-Object { -not $before.ContainsKey($_.FullName) } |
            Sort-Object Length
    )
    if ($newCaptures.Count -lt 1) {
        throw 'DOSBox-X did not produce a new AVI capture.'
    }
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    $fullCapture = $newCaptures[-1]
    Move-Item -LiteralPath $fullCapture.FullName -Destination $fullOutput
    if ($newCaptures.Count -gt 1) {
        $modeSwitchCapture = $newCaptures[0]
        Move-Item -LiteralPath $modeSwitchCapture.FullName -Destination $switchOutput
    }
    Get-Item -LiteralPath $fullOutput
}
finally {
    if ($processId) {
        $process = Get-Process -Id $processId -ErrorAction SilentlyContinue
        if ($process) {
            $windows = Get-SceneWindows ([uint32]$processId)
            if ($windows.Game -ne [IntPtr]::Zero) {
                [void][WordSceneCaptureNative]::PostMessageW(
                    $windows.Game, 0x0111, [UIntPtr]4100, [IntPtr]::Zero)
            }
            if (-not $process.WaitForExit(3000)) {
                Stop-Process -Id $processId -Force
            }
        }
    }
}
