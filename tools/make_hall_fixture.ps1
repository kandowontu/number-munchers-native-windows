param(
    [string]$InputPath = (Join-Path $PSScriptRoot '..\analysis\original-NM-CFG-before-name-probe.bin'),
    [string]$OutputPath = (Join-Path $PSScriptRoot '..\analysis\NM-CFG-full-hall-fixture.bin'),
    [ValidateRange(0, 5)]
    [int]$Mode = 4
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $InputPath).Path
$bytes = [IO.File]::ReadAllBytes($source)

$hallOffset = 0x160
$hallBlockSize = 0x132
$hallHeaderSize = 6
$hallEntrySize = 30
$nameBytes = 26
$block = $hallOffset + $Mode * $hallBlockSize
if ($bytes.Length -lt $block + $hallBlockSize) {
    throw "Configuration is too short for Hall block $Mode"
}

$entries = @(
    @{ Name = 'Alpha';   Score = 500 },
    @{ Name = 'Beta';    Score = 450 },
    @{ Name = 'Gamma';   Score = 400 },
    @{ Name = 'Delta';   Score = 350 },
    @{ Name = 'Epsilon'; Score = 300 },
    @{ Name = 'Zeta';    Score = 250 },
    @{ Name = 'Eta';     Score = 200 },
    @{ Name = 'Theta';   Score = 150 },
    @{ Name = 'Iota';    Score = 100 },
    @{ Name = 'Kappa';   Score = 50 }
)

[Array]::Clear($bytes, $block + $hallHeaderSize, $hallBlockSize - $hallHeaderSize)
$bytes[$block] = [byte]$entries.Count
$bytes[$block + 1] = 0
for ($index = 0; $index -lt $entries.Count; ++$index) {
    $entry = $block + $hallHeaderSize + $index * $hallEntrySize
    $encodedName = [Text.Encoding]::ASCII.GetBytes([string]$entries[$index].Name)
    if ($encodedName.Length -gt 25) { throw "Hall name is longer than 25 bytes" }
    [Array]::Copy($encodedName, 0, $bytes, $entry, $encodedName.Length)
    $score = [BitConverter]::GetBytes([uint32]$entries[$index].Score)
    [Array]::Copy($score, 0, $bytes, $entry + $nameBytes, $score.Length)
}

$destination = [IO.Path]::GetFullPath($OutputPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
[IO.File]::WriteAllBytes($destination, $bytes)
[pscustomobject]@{
    source = $source
    output = $destination
    mode = $Mode
    entries = $entries.Count
    sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
}
