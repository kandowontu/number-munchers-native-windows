param(
    [Parameter(Mandatory)]
    [ValidateRange(0, 4)]
    [int]$SceneIndex,
    [double]$RunSeconds = 28.0,
    [switch]$AllowVisibleWindow
)

$ErrorActionPreference = 'Stop'
if (-not $AllowVisibleWindow) {
    throw 'Original Number scene capture requires explicit -AllowVisibleWindow approval.'
}

$workspace = [System.IO.Path]::GetFullPath(
    (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)))
$captureDirectory = Join-Path $workspace 'analysis\captures'
$outputDirectory = Join-Path $workspace 'analysis\number-live\scenes\original'
$variantDirectory = Join-Path $workspace (
    'analysis\number-live\scenes\reference-control\scene-{0}' -f $SceneIndex)
$fullOutput = Join-Path $outputDirectory ('scene-{0}-full.avi' -f $SceneIndex)
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
if (-not (Test-Path -LiteralPath (Join-Path $variantDirectory 'NM.EXE') -PathType Leaf)) {
    throw "Missing scene-control variant: $variantDirectory"
}

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class NumberSceneCaptureNative {
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
    $callback = [NumberSceneCaptureNative+WindowCallback] {
        param([IntPtr]$window, [IntPtr]$value)
        [uint32]$foundProcessId = 0
        [void][NumberSceneCaptureNative]::GetWindowThreadProcessId(
            $window, [ref]$foundProcessId)
        if ($foundProcessId -eq $ProcessId) {
            $title = [Text.StringBuilder]::new(256)
            $className = [Text.StringBuilder]::new(256)
            [void][NumberSceneCaptureNative]::GetWindowText($window, $title, 256)
            [void][NumberSceneCaptureNative]::GetClassName($window, $className, 256)
            if ($className.ToString() -eq 'SDL_app') {
                $result.Game = $window
            } elseif ($title.ToString() -eq 'DOSBox-X Debugger') {
                $result.Debugger = $window
            }
        }
        return $true
    }
    [void][NumberSceneCaptureNative]::EnumWindows($callback, [IntPtr]::Zero)
    return $result
}

$before = @{}
Get-ChildItem -LiteralPath $captureDirectory -Filter 'nm_*.avi' -File |
    ForEach-Object { $before[$_.FullName] = $true }
$processId = $null
try {
    & (Join-Path $workspace 'tools\launch_original.ps1') `
        -Game Number `
        -MountDirectory $variantDirectory `
        -Sequence 'space space enter' `
        -InitialWaitSeconds 1 `
        -PaceSeconds 2 `
        -StartupWaitSeconds 7 `
        -KeepOnscreen `
        -AllowVisibleWindow | Out-Host

    $processId = [int](Get-Content -LiteralPath $pidPath)
    $process = Get-Process -Id $processId
    $windows = Get-SceneWindows ([uint32]$processId)
    if ($windows.Game -eq [IntPtr]::Zero) {
        throw 'Could not find the DOSBox-X SDL game window.'
    }

    # Pause DOSBox-X and place one Enter in the BIOS keyboard ring to accept the
    # default-No instruction question. The controller clears the ring while
    # constructing its game selector, so that selector receives a second Enter
    # in a separate pause below. No host raw-input injection or cartoon-code
    # modification is involved.
    [void][NumberSceneCaptureNative]::PostMessageW(
        $windows.Game, 0x0111, [UIntPtr]5255, [IntPtr]::Zero)
    for ($attempt = 0; $attempt -lt 40; ++$attempt) {
        Start-Sleep -Milliseconds 100
        $windows = Get-SceneWindows ([uint32]$processId)
        if ($windows.Debugger -ne [IntPtr]::Zero) { break }
    }
    if ($windows.Debugger -eq [IntPtr]::Zero) {
        throw 'Could not find the DOSBox-X debugger console.'
    }
    [void][NumberSceneCaptureNative]::ShowWindow($windows.Debugger, 9)
    [void][NumberSceneCaptureNative]::SetForegroundWindow($windows.Debugger)
    $shell = New-Object -ComObject WScript.Shell
    Start-Sleep -Milliseconds 300
    $shell.SendKeys('sm 0040:001a 1e 00 20 00 0d 1c{ENTER}')
    Start-Sleep -Milliseconds 500
    $shell.SendKeys('{F5}')
    # The instruction controller clears the BIOS ring before it finishes
    # constructing the game selector.  Give the resumed program enough time to
    # reach the stable selector before pausing for the second injection; the
    # previous one-second edge was capture-host dependent and intermittently
    # landed before that clear.
    Start-Sleep -Milliseconds 2500

    [void][NumberSceneCaptureNative]::PostMessageW(
        $windows.Game, 0x0111, [UIntPtr]5255, [IntPtr]::Zero)
    for ($attempt = 0; $attempt -lt 40; ++$attempt) {
        Start-Sleep -Milliseconds 100
        $windows = Get-SceneWindows ([uint32]$processId)
        if ($windows.Debugger -ne [IntPtr]::Zero) { break }
    }
    if ($windows.Debugger -eq [IntPtr]::Zero) {
        throw 'Could not reopen the DOSBox-X debugger console at the game selector.'
    }
    [void][NumberSceneCaptureNative]::ShowWindow($windows.Debugger, 9)
    [void][NumberSceneCaptureNative]::SetForegroundWindow($windows.Debugger)
    Start-Sleep -Milliseconds 300
    $shell.SendKeys('sm 0040:001a 1e 00 20 00 0d 1c{ENTER}')
    Start-Sleep -Milliseconds 500
    # Start a fresh recording only after both debugger-assisted selector
    # injections are complete. DOSBox-X finalizes an active AVI when entering
    # the debugger, so launching through dx-capture here would preserve only
    # the startup screens and silently miss the requested cartoon.
    [void][NumberSceneCaptureNative]::PostMessageW(
        $windows.Game, 0x0111, [UIntPtr]5217, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
    $shell.SendKeys('{F5}')
    Start-Sleep -Seconds 1
    [void][NumberSceneCaptureNative]::ShowWindow($windows.Debugger, 0)
    [void][NumberSceneCaptureNative]::ShowWindow($windows.Game, 9)

    Start-Sleep -Milliseconds ([int][Math]::Round($RunSeconds * 1000.0))

    [void][NumberSceneCaptureNative]::PostMessageW(
        $windows.Game, 0x0111, [UIntPtr]5217, [IntPtr]::Zero)
    Start-Sleep -Seconds 2

    $newCaptures = @(
        Get-ChildItem -LiteralPath $captureDirectory -Filter 'nm_*.avi' -File |
            Where-Object { -not $before.ContainsKey($_.FullName) } |
            Sort-Object Length
    )
    if ($newCaptures.Count -lt 1) {
        throw 'DOSBox-X did not produce a new AVI capture.'
    }
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    $fullCapture = $newCaptures | Sort-Object LastWriteTime | Select-Object -Last 1
    Move-Item -LiteralPath $fullCapture.FullName -Destination $fullOutput
    Get-Item -LiteralPath $fullOutput
}
finally {
    if ($processId) {
        $process = Get-Process -Id $processId -ErrorAction SilentlyContinue
        if ($process) {
            $windows = Get-SceneWindows ([uint32]$processId)
            if ($windows.Game -ne [IntPtr]::Zero) {
                [void][NumberSceneCaptureNative]::PostMessageW(
                    $windows.Game, 0x0111, [UIntPtr]4100, [IntPtr]::Zero)
            }
            if (-not $process.WaitForExit(3000)) {
                Stop-Process -Id $processId -Force
            }
        }
    }
}
