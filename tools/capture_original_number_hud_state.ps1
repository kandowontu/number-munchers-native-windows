param(
    [string]$OutputFile = 'analysis\number-live\progression\original-number-level7-score321.avi',
    [int]$Level = 7,
    [int]$Score = 321,
    [int]$Reserves = 3,
    [double]$RunSecondsAfterWrite = 5.0,
    [switch]$AllowVisibleWindow
)

$ErrorActionPreference = 'Stop'
if (-not $AllowVisibleWindow) {
    throw 'Original HUD-state capture requires explicit -AllowVisibleWindow approval.'
}

$workspace = [IO.Path]::GetFullPath(
    (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)))
$captureDirectory = [IO.Path]::GetFullPath((Join-Path $workspace 'analysis\captures'))
$output = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($OutputFile)) {
    $OutputFile
} else {
    Join-Path $workspace $OutputFile
}))
$outputDirectory = Split-Path -Parent $output
foreach ($path in @($captureDirectory, $outputDirectory, $output)) {
    if (-not $path.StartsWith($workspace, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Capture path escaped the workspace: $path"
    }
}
if (Test-Path -LiteralPath $output) {
    throw "Output already exists: $output"
}
$stateOutput = [IO.Path]::ChangeExtension($output, '.state.txt')
if (Test-Path -LiteralPath $stateOutput) {
    throw "State output already exists: $stateOutput"
}

if (-not ('OriginalNumberHudCaptureNative' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OriginalNumberHudCaptureNative {
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(
        IntPtr window, uint message, UIntPtr word, IntPtr value);
}
'@
}

$beforeCaptures = @{}
Get-ChildItem -LiteralPath $captureDirectory -Filter 'nm_*.avi' -File |
    ForEach-Object { $beforeCaptures[$_.FullName] = $true }
$beforeProcessIds = @(
    (Get-Process -ErrorAction SilentlyContinue |
        Where-Object ProcessName -Match '^(dosbox|dosbox-x)$').Id)
$captureProcessId = $null
$capturePidFile = Join-Path $workspace (
    'analysis\dosbox-number-hud-{0}-{1}.pid' -f
        $PID, [Guid]::NewGuid().ToString('N'))
try {
    # The three-second pace deliberately separates each synchronous controller:
    # version, splash, Play, default-No instruction question, and Multiples.
    & (Join-Path $workspace 'tools\launch_original.ps1') `
        -Game Number `
        -GraphicsMode VGA `
        -Sequence 'space space enter enter enter' `
        -InitialWaitSeconds 1 `
        -PaceSeconds 3 `
        -StartupWaitSeconds 5 `
        -PidFile $capturePidFile `
        -CaptureVideo `
        -KeepMinimized `
        -AllowVisibleWindow | Out-Host

    $captureProcessId = [int](Get-Content -LiteralPath $capturePidFile)
    $injectionOutput = @(& python (Join-Path $workspace 'tools\audit_number_live_memory.py') `
        --pid $captureProcessId `
        --seed 0x1cb5 `
        --wait-for-user-game-seconds 15 `
        --level $Level `
        --score $Score `
        --reserves $Reserves 2>&1)
    $injectionOutput | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Number HUD-state injection failed with exit code $LASTEXITCODE."
    }
    [IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
    [IO.File]::WriteAllLines($stateOutput, [string[]]$injectionOutput)
    Start-Sleep -Milliseconds ([int][Math]::Round($RunSecondsAfterWrite * 1000.0))

    $captureProcess = Get-Process -Id $captureProcessId -ErrorAction SilentlyContinue
    if ($captureProcess) {
        [void][OriginalNumberHudCaptureNative]::PostMessageW(
            [IntPtr]$captureProcess.MainWindowHandle,
            0x0111,
            [UIntPtr]4100,
            [IntPtr]::Zero)
        if (-not $captureProcess.WaitForExit(5000)) {
            Stop-Process -Id $captureProcessId -Force
        }
    }
    Start-Sleep -Seconds 2

    $newCaptures = @(
        Get-ChildItem -LiteralPath $captureDirectory -Filter 'nm_*.avi' -File |
            Where-Object { -not $beforeCaptures.ContainsKey($_.FullName) } |
            Sort-Object Length -Descending)
    if ($newCaptures.Count -eq 0) {
        throw 'DOSBox-X did not produce a new Number AVI.'
    }
    if ($newCaptures.Count -gt 1 -and
        $newCaptures[0].Length -lt (4 * $newCaptures[1].Length)) {
        throw 'No unambiguous main Number AVI was produced; preserving all segments.'
    }
    Move-Item -LiteralPath $newCaptures[0].FullName -Destination $output
    Get-Item -LiteralPath $output, $stateOutput
}
finally {
    if ($captureProcessId) {
        $ownedProcess = Get-Process -Id $captureProcessId -ErrorAction SilentlyContinue
        if ($ownedProcess -and
            $ownedProcess.ProcessName -match '^(dosbox|dosbox-x)$' -and
            $captureProcessId -notin $beforeProcessIds) {
            Stop-Process -Id $captureProcessId -Force -ErrorAction SilentlyContinue
        }
    }
    Remove-Item -LiteralPath $capturePidFile -Force -ErrorAction SilentlyContinue
}
