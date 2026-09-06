param(
    [ValidateSet('Number', 'Word', 'Super')]
    [string]$Game = 'Number',
    [ValidateSet('VGA', 'CGA')]
    [string]$GraphicsMode = 'VGA',
    [string]$Sequence = '',
    [double]$InitialWaitSeconds = 8.0,
    [double]$PaceSeconds = 0.8,
    [double]$StartupWaitSeconds = 10,
    [string]$MountDirectory = '',
    [string]$PidFile = '',
    [switch]$CaptureVideo,
    [switch]$KeepMinimized,
    [switch]$KeepOnscreen,
    [switch]$AllowVisibleWindow
)

if (-not $AllowVisibleWindow) {
    throw 'DOSBox launch blocked: pass -AllowVisibleWindow only after explicit user approval.'
}

$workspace = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$pidFile = if ($PidFile) {
    [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($PidFile)) {
        $PidFile
    } else {
        Join-Path $workspace $PidFile
    }))
} else {
    Join-Path $workspace 'analysis\dosbox.pid'
}
if (-not $pidFile.StartsWith($workspace, [StringComparison]::OrdinalIgnoreCase)) {
    throw "PID file escaped the workspace: $pidFile"
}
if (Test-Path -LiteralPath $pidFile) {
    $oldId = [int](Get-Content -LiteralPath $pidFile)
    $oldProcess = Get-Process -Id $oldId -ErrorAction SilentlyContinue
    if ($oldProcess -and $oldProcess.ProcessName -match '^(dosbox|dosbox-x)$') {
        Stop-Process -Id $oldId -Force -ErrorAction SilentlyContinue
    }
}

$baseConfig = Join-Path $workspace 'analysis\dosbox-x.conf'
$runtimeConfig = Join-Path $workspace $(switch ($Game) {
    'Word' { 'analysis\dosbox-x-runtime-word.conf' }
    'Super' { 'analysis\dosbox-x-runtime-super.conf' }
    default { 'analysis\dosbox-x-runtime.conf' }
})
$lines = [System.Collections.Generic.List[string]](Get-Content -LiteralPath $baseConfig)
$machineLine = -1
for ($index = 0; $index -lt $lines.Count; ++$index) {
    if ($lines[$index].StartsWith('machine=', [StringComparison]::OrdinalIgnoreCase)) {
        $machineLine = $index
        break
    }
}
if ($machineLine -lt 0) {
    throw 'Could not locate the DOSBox machine line in the base config'
}
$lines[$machineLine] = if ($GraphicsMode -eq 'CGA') { 'machine=cga' } else { 'machine=svga_s3' }
$launchLine = $lines.IndexOf('nm nomouse')
if ($launchLine -lt 0) {
    throw 'Could not locate the game launch line in the base config'
}
$command = 'nm nomouse'
if ($MountDirectory -or $Game -ne 'Number') {
    $mountLine = -1
    for ($index = 0; $index -lt $lines.Count; ++$index) {
        if ($lines[$index].StartsWith('mount c ', [StringComparison]::OrdinalIgnoreCase)) {
            $mountLine = $index
            break
        }
    }
    if ($mountLine -lt 0) { throw 'Could not locate the DOSBox mount line.' }
    $gameDirectory = if ($MountDirectory) {
        [System.IO.Path]::GetFullPath($MountDirectory)
    } else {
        switch ($Game) {
            'Word' { Join-Path $workspace 'extracted\word-munchers\WM' }
            'Super' {
                Join-Path $workspace `
                    'tmp\super-munchers-source\super-munchers-the-challenge-continues\smunch'
            }
            default { Join-Path $workspace 'extracted\disk\NMUNCH' }
        }
    }
    $guestExecutable = switch ($Game) {
        'Word' { 'WM.EXE' }
        'Super' { 'SM.EXE' }
        default { 'NM.EXE' }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $gameDirectory $guestExecutable) -PathType Leaf)) {
        throw "$Game mount directory does not contain ${guestExecutable}: $gameDirectory"
    }
    $lines[$mountLine] = 'mount c "{0}"' -f $gameDirectory
}
if ($Game -ne 'Number') {
    $command = if ($Game -eq 'Word') { 'wm nomouse' } else { 'sm nomouse' }
    $lines[$launchLine] = $command
}
if ($CaptureVideo) {
    $lines[$launchLine] = 'dx-capture /V /-D {0}' -f $command
}
if ($Sequence) {
    $autotype = 'autotype -w {0} -p {1} {2}' -f $InitialWaitSeconds, $PaceSeconds, $Sequence
    $lines.Insert($launchLine, $autotype)
}
[System.IO.File]::WriteAllLines($runtimeConfig, $lines)

$executable = Join-Path $workspace 'tools\_third_party\dosbox-x\mingw-build\mingw-sdl2\dosbox-x.exe'
$arguments = '-conf "{0}"' -f $runtimeConfig
$process = Start-Process -FilePath $executable -ArgumentList $arguments -WorkingDirectory $workspace -WindowStyle Minimized -PassThru
Set-Content -LiteralPath $pidFile -Value $process.Id
if (-not $KeepMinimized) {
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class OriginalLauncherNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hwnd, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rectangle);
}
'@
for ($attempt = 0; $attempt -lt 50; $attempt++) {
    Start-Sleep -Milliseconds 100
    $process.Refresh()
    if ($process.HasExited) { break }
    if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
        $window = [IntPtr]$process.MainWindowHandle
        [OriginalLauncherNative]::ShowWindow($window, 9) | Out-Null
        Start-Sleep -Milliseconds 100
        $rectangle = New-Object OriginalLauncherNative+RECT
        [OriginalLauncherNative]::GetWindowRect($window, [ref]$rectangle) | Out-Null
        $width = $rectangle.Right - $rectangle.Left
        $height = $rectangle.Bottom - $rectangle.Top
        if (-not $KeepOnscreen) {
            [OriginalLauncherNative]::MoveWindow($window, -2000, 20, $width, $height, $true) | Out-Null
        }
        break
    }
}
} else {
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        if ($process.HasExited -or $process.MainWindowHandle -ne [IntPtr]::Zero) { break }
    }
}
Start-Sleep -Seconds $StartupWaitSeconds
$process.Refresh()
$process | Select-Object Id, HasExited, MainWindowHandle, MainWindowTitle
