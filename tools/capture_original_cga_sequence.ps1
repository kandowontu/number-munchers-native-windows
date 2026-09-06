param(
    [Parameter(Mandatory)]
    [ValidateSet('Number', 'Word', 'Super')]
    [string]$Game,
    [Parameter(Mandatory)]
    [string]$Sequence,
    [Parameter(Mandatory)]
    [string]$OutputDirectory,
    [Parameter(Mandatory)]
    [string]$OutputStem,
    [string]$MountDirectory = '',
    [double]$InitialWaitSeconds = 1.0,
    [double]$PaceSeconds = 1.0,
    [double]$RunSeconds = 8.0,
    [switch]$AllowVisibleWindow
)

$ErrorActionPreference = 'Stop'
if (-not $AllowVisibleWindow) {
    throw 'Original CGA capture requires explicit -AllowVisibleWindow approval.'
}
if ($OutputStem -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') {
    throw "Invalid output stem: $OutputStem"
}

$workspace = [IO.Path]::GetFullPath(
    (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)))
$resolvedMountDirectory = ''
if ($MountDirectory) {
    $resolvedMountDirectory = [IO.Path]::GetFullPath(
        (Join-Path $workspace $MountDirectory))
    if (-not $resolvedMountDirectory.StartsWith(
            $workspace, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Mount directory escaped the workspace: $resolvedMountDirectory"
    }
}
$captureDirectory = [IO.Path]::GetFullPath(
    (Join-Path $workspace 'analysis\captures'))
$resolvedOutputDirectory = [IO.Path]::GetFullPath(
    (Join-Path $workspace $OutputDirectory))
$output = [IO.Path]::GetFullPath(
    (Join-Path $resolvedOutputDirectory ($OutputStem + '.avi')))
$modeSwitchOutput = [IO.Path]::GetFullPath(
    (Join-Path $resolvedOutputDirectory ($OutputStem + '-mode-switch.avi')))
$invalidModeSwitchOutput = [IO.Path]::GetFullPath(
    (Join-Path $resolvedOutputDirectory ($OutputStem + '-mode-switch-invalid.avi')))
foreach ($path in @(
    $captureDirectory, $resolvedOutputDirectory, $output, $modeSwitchOutput,
    $invalidModeSwitchOutput)) {
    if (-not $path.StartsWith($workspace, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Capture path escaped the workspace: $path"
    }
}
foreach ($path in @($output, $modeSwitchOutput, $invalidModeSwitchOutput)) {
    if (Test-Path -LiteralPath $path) { throw "Output already exists: $path" }
}

if (-not ('OriginalCgaSequenceCaptureNative' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OriginalCgaSequenceCaptureNative {
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(
        IntPtr window, uint message, UIntPtr word, IntPtr value);
}
'@
}

$before = @{}
Get-ChildItem -LiteralPath $captureDirectory -Filter '*.avi' -File |
    ForEach-Object { $before[$_.FullName] = $true }
$beforeProcessIds = @(
    (Get-Process -ErrorAction SilentlyContinue |
        Where-Object ProcessName -Match '^(dosbox|dosbox-x)$').Id)
$captureProcessId = $null
$capturePidFile = Join-Path $workspace (
    'analysis\dosbox-cga-capture-{0}-{1}.pid' -f $PID, [Guid]::NewGuid().ToString('N'))
try {
    & (Join-Path $workspace 'tools\launch_original.ps1') `
        -Game $Game `
        -GraphicsMode CGA `
        -MountDirectory $resolvedMountDirectory `
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
        [void][OriginalCgaSequenceCaptureNative]::PostMessageW(
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
        Get-ChildItem -LiteralPath $captureDirectory -Filter '*.avi' -File |
            Where-Object { -not $before.ContainsKey($_.FullName) } |
            Sort-Object LastWriteTime)
    if ($newCaptures.Count -lt 1 -or $newCaptures.Count -gt 2) {
        throw "Expected one CGA AVI with an optional mode-switch AVI, found $($newCaptures.Count)."
    }

    $cgaCaptures = @()
    $modeSwitchCaptures = @()
    $invalidCaptures = @()
    foreach ($candidate in $newCaptures) {
        $probeText = @(& ffprobe -v error -select_streams v:0 `
            -show_entries stream=width,height -of json $candidate.FullName `
            2>$null)
        if ($LASTEXITCODE -ne 0 -or -not $probeText) {
            $invalidCaptures += $candidate
            continue
        }
        try {
            $probe = ($probeText -join [Environment]::NewLine) |
                ConvertFrom-Json
        }
        catch {
            $invalidCaptures += $candidate
            continue
        }
        $stream = $probe.streams | Select-Object -First 1
        if ($stream.width -eq 320 -and $stream.height -eq 200) {
            $cgaCaptures += $candidate
        } elseif ($stream.width -eq 640 -and $stream.height -eq 400) {
            $modeSwitchCaptures += $candidate
        } else {
            $invalidCaptures += $candidate
        }
    }
    if ($cgaCaptures.Count -ne 1) {
        throw "Expected exactly one 320x200 CGA segment, found $($cgaCaptures.Count)."
    }
    if ($modeSwitchCaptures.Count -gt 1) {
        throw "Expected at most one 640x400 mode-switch segment, found $($modeSwitchCaptures.Count)."
    }
    if ($invalidCaptures.Count -gt 1) {
        throw "Expected at most one invalid auxiliary segment, found $($invalidCaptures.Count)."
    }
    if ($modeSwitchCaptures.Count -and $invalidCaptures.Count) {
        throw 'Found both a valid and an invalid auxiliary mode-switch segment.'
    }
    if (-not $cgaCaptures) {
        throw 'Could not identify the 320x200 CGA segment.'
    }

    [IO.Directory]::CreateDirectory($resolvedOutputDirectory) | Out-Null
    if ($modeSwitchCaptures.Count) {
        Move-Item -LiteralPath $modeSwitchCaptures[0].FullName `
            -Destination $modeSwitchOutput
    }
    if ($invalidCaptures.Count) {
        Move-Item -LiteralPath $invalidCaptures[0].FullName `
            -Destination $invalidModeSwitchOutput
    }
    Move-Item -LiteralPath $cgaCaptures[0].FullName -Destination $output
    @($invalidModeSwitchOutput, $modeSwitchOutput, $output) |
        Where-Object { Test-Path -LiteralPath $_ } |
        ForEach-Object { Get-Item -LiteralPath $_ }
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
