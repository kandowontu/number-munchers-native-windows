param(
    [Parameter(Mandatory)]
    [ValidateSet('Number', 'Word', 'Super')]
    [string]$Game,
    [Parameter(Mandatory)]
    [string]$Sequence,
    [Parameter(Mandatory)]
    [string]$OutputFile,
    [double]$InitialWaitSeconds = 1.0,
    [double]$PaceSeconds = 0.8,
    [double]$RunSeconds = 10.0,
    [switch]$AllowVisibleWindow
)

$ErrorActionPreference = 'Stop'
if (-not $AllowVisibleWindow) {
    throw 'Original VGA capture requires explicit -AllowVisibleWindow approval.'
}

$workspace = [IO.Path]::GetFullPath(
    (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)))
$captureDirectory = [IO.Path]::GetFullPath(
    (Join-Path $workspace 'analysis\captures'))
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

if (-not ('OriginalVgaSequenceCaptureNative' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OriginalVgaSequenceCaptureNative {
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(
        IntPtr window, uint message, UIntPtr word, IntPtr value);
}
'@
}

$pattern = switch ($Game) {
    'Word' { 'wm_*.avi' }
    'Super' { 'sm_*.avi' }
    default { 'nm_*.avi' }
}
$before = @{}
Get-ChildItem -LiteralPath $captureDirectory -Filter $pattern -File |
    ForEach-Object { $before[$_.FullName] = $true }
$beforeProcessIds = @(
    (Get-Process -ErrorAction SilentlyContinue |
        Where-Object ProcessName -Match '^(dosbox|dosbox-x)$').Id)
$captureProcessId = $null
$capturePidFile = Join-Path $workspace (
    'analysis\dosbox-vga-capture-{0}-{1}.pid' -f
        $PID, [Guid]::NewGuid().ToString('N'))
try {
    & (Join-Path $workspace 'tools\launch_original.ps1') `
        -Game $Game `
        -GraphicsMode VGA `
        -Sequence $Sequence `
        -InitialWaitSeconds $InitialWaitSeconds `
        -PaceSeconds $PaceSeconds `
        -StartupWaitSeconds $RunSeconds `
        -PidFile $capturePidFile `
        -CaptureVideo `
        -KeepMinimized `
        -AllowVisibleWindow | Out-Host

    $captureProcessId = [int](Get-Content -LiteralPath $capturePidFile)
    $captureProcess = Get-Process -Id $captureProcessId -ErrorAction SilentlyContinue
    if ($captureProcess) {
        [void][OriginalVgaSequenceCaptureNative]::PostMessageW(
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
        Get-ChildItem -LiteralPath $captureDirectory -Filter $pattern -File |
            Where-Object { -not $before.ContainsKey($_.FullName) } |
            Sort-Object LastWriteTime)
    if ($newCaptures.Count -eq 0) {
        throw "Expected a new $Game VGA AVI, found none."
    }
    $rankedCaptures = @($newCaptures | Sort-Object Length -Descending)
    $mainCapture = $rankedCaptures[0]
    if ($rankedCaptures.Count -gt 1 -and
        $mainCapture.Length -lt (4 * $rankedCaptures[1].Length)) {
        throw ("Found {0} new {1} VGA AVIs without one unambiguous main " +
            "segment; preserving all captures for inspection." -f
                $rankedCaptures.Count, $Game)
    }
    [IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
    Move-Item -LiteralPath $mainCapture.FullName -Destination $output
    $auxiliaryCaptures = @(
        $rankedCaptures | Where-Object FullName -ne $mainCapture.FullName)
    if ($auxiliaryCaptures.Count -gt 0) {
        Write-Host ("Preserved {0} small startup mode segment(s) in {1}." -f
            $auxiliaryCaptures.Count, $captureDirectory)
    }
    Get-Item -LiteralPath $output
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
