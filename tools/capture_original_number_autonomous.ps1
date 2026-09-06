param(
    [string]$OutputFile = 'analysis\number-live\attract-continuation\original-number-autonomous-f585.avi',
    [double]$RunSeconds = 365.0,
    [switch]$AllowVisibleWindow
)

$ErrorActionPreference = 'Stop'
if (-not $AllowVisibleWindow) {
    throw 'Original autonomous capture requires explicit -AllowVisibleWindow approval.'
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
$stateOutput = [IO.Path]::ChangeExtension($output, '.state.txt')
foreach ($path in @($captureDirectory, $outputDirectory, $output, $stateOutput)) {
    if (-not $path.StartsWith($workspace, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Capture path escaped the workspace: $path"
    }
}
foreach ($path in @($output, $stateOutput)) {
    if (Test-Path -LiteralPath $path) {
        throw "Output already exists: $path"
    }
}

if (-not ('OriginalNumberAutonomousCaptureNative' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OriginalNumberAutonomousCaptureNative {
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
    'analysis\dosbox-number-autonomous-{0}-{1}.pid' -f
        $PID, [Guid]::NewGuid().ToString('N'))
try {
    # Reach the title through DOSBox-X's own key queue, then leave it idle.
    & (Join-Path $workspace 'tools\launch_original.ps1') `
        -Game Number `
        -GraphicsMode VGA `
        -Sequence 'space space' `
        -InitialWaitSeconds 1 `
        -PaceSeconds 2 `
        -StartupWaitSeconds 5 `
        -PidFile $capturePidFile `
        -KeepMinimized `
        -AllowVisibleWindow | Out-Host

    $captureProcessId = [int](Get-Content -LiteralPath $capturePidFile)
    $seedOutput = @(& python (Join-Path $workspace 'tools\audit_number_live_memory.py') `
        --pid $captureProcessId `
        --seed 0xf585 `
        --wait-for-demo-seconds 60 2>&1)
    $seedOutput | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Number autonomous seed installation failed with exit code $LASTEXITCODE."
    }

    [IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
    [IO.File]::WriteAllLines(
        $stateOutput,
        [string[]](@('requested_seed=0xF585') + $seedOutput))

    $captureProcess = Get-Process -Id $captureProcessId -ErrorAction Stop
    [void][OriginalNumberAutonomousCaptureNative]::PostMessageW(
        [IntPtr]$captureProcess.MainWindowHandle,
        0x0111,
        [UIntPtr]5217,
        [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    Start-Sleep -Milliseconds ([int][Math]::Round($RunSeconds * 1000.0))
    [void][OriginalNumberAutonomousCaptureNative]::PostMessageW(
        [IntPtr]$captureProcess.MainWindowHandle,
        0x0111,
        [UIntPtr]5217,
        [IntPtr]::Zero)
    Start-Sleep -Seconds 2

    [void][OriginalNumberAutonomousCaptureNative]::PostMessageW(
        [IntPtr]$captureProcess.MainWindowHandle,
        0x0111,
        [UIntPtr]4100,
        [IntPtr]::Zero)
    if (-not $captureProcess.WaitForExit(5000)) {
        Stop-Process -Id $captureProcessId -Force
    }
    Start-Sleep -Seconds 2

    $newCaptures = @(
        Get-ChildItem -LiteralPath $captureDirectory -Filter 'nm_*.avi' -File |
            Where-Object { -not $beforeCaptures.ContainsKey($_.FullName) } |
            Sort-Object Length -Descending)
    if ($newCaptures.Count -ne 1) {
        throw "Expected exactly one new autonomous AVI, found $($newCaptures.Count)."
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
