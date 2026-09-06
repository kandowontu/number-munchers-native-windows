param(
    [string]$InputPath = (Join-Path $PSScriptRoot "..\analysis\NM-unpacked.exe"),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\analysis"),
    [string]$Prefix = "NM-unpacked"
)

$ErrorActionPreference = "Stop"
$InputPath = (Resolve-Path $InputPath).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path $OutputDirectory).Path

$bytes = [System.IO.File]::ReadAllBytes($InputPath)
if ($bytes.Length -lt 64 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
    throw "Input is not an MZ executable: $InputPath"
}

function Read-U16([int]$Offset) {
    return [BitConverter]::ToUInt16($bytes, $Offset)
}

$lastPageBytes = Read-U16 2
$pageCount = Read-U16 4
$relocationCount = Read-U16 6
$headerParagraphs = Read-U16 8
$headerBytes = $headerParagraphs * 16
$declaredBytes = $pageCount * 512
if ($lastPageBytes -ne 0) { $declaredBytes -= 512 - $lastPageBytes }
if ($headerBytes -gt $bytes.Length -or $declaredBytes -gt $bytes.Length) {
    throw "Invalid MZ size fields."
}

$imageLength = $declaredBytes - $headerBytes
$image = [byte[]]::new($imageLength)
[Array]::Copy($bytes, $headerBytes, $image, 0, $imageLength)
$imagePath = Join-Path $OutputDirectory "$Prefix-image.bin"
[System.IO.File]::WriteAllBytes($imagePath, $image)

$ndisasm = (Get-Command ndisasm -ErrorAction Stop).Source
$processInfo = [Diagnostics.ProcessStartInfo]::new()
$processInfo.FileName = $ndisasm
$processInfo.ArgumentList.Add("-b")
$processInfo.ArgumentList.Add("16")
$processInfo.ArgumentList.Add("-a")
$processInfo.ArgumentList.Add($imagePath)
$processInfo.UseShellExecute = $false
$processInfo.RedirectStandardOutput = $true
$processInfo.RedirectStandardError = $true
$process = [Diagnostics.Process]::Start($processInfo)
$assembly = $process.StandardOutput.ReadToEnd()
$errorText = $process.StandardError.ReadToEnd()
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw "ndisasm failed: $errorText" }
$assemblyPath = Join-Path $OutputDirectory "$Prefix.ndisasm"
[System.IO.File]::WriteAllText($assemblyPath, $assembly, [Text.UTF8Encoding]::new($false))

$strings = (Get-Command strings -ErrorAction Stop).Source
$stringsInfo = [Diagnostics.ProcessStartInfo]::new()
$stringsInfo.FileName = $strings
$stringsInfo.ArgumentList.Add("-n")
$stringsInfo.ArgumentList.Add("4")
$stringsInfo.ArgumentList.Add($InputPath)
$stringsInfo.UseShellExecute = $false
$stringsInfo.RedirectStandardOutput = $true
$stringsInfo.RedirectStandardError = $true
$stringsProcess = [Diagnostics.Process]::Start($stringsInfo)
$stringsText = $stringsProcess.StandardOutput.ReadToEnd()
$stringsError = $stringsProcess.StandardError.ReadToEnd()
$stringsProcess.WaitForExit()
if ($stringsProcess.ExitCode -ne 0) { throw "strings failed: $stringsError" }
$stringsPath = Join-Path $OutputDirectory "$Prefix-strings.txt"
[System.IO.File]::WriteAllText($stringsPath, $stringsText, [Text.UTF8Encoding]::new($false))

$relocationTableOffset = Read-U16 24
$relocations = for ($index = 0; $index -lt $relocationCount; $index++) {
    $record = $relocationTableOffset + $index * 4
    $offset = Read-U16 $record
    $segment = Read-U16 ($record + 2)
    [ordered]@{ segment = $segment; offset = $offset; image_linear = $segment * 16 + $offset }
}

$lines = $assembly -split "`r?`n"
$callLines = @($lines | Where-Object { $_ -match '\bcall\b' })
$interruptLines = @($lines | Where-Object { $_ -match '\bint\s+0x' })
$summary = [ordered]@{
    source = $InputPath
    source_sha256 = (Get-FileHash $InputPath -Algorithm SHA256).Hash
    mz = [ordered]@{
        file_bytes = $bytes.Length
        declared_file_bytes = $declaredBytes
        header_bytes = $headerBytes
        load_image_bytes = $imageLength
        relocation_count = $relocationCount
        relocation_table_offset = $relocationTableOffset
        minimum_extra_paragraphs = Read-U16 10
        maximum_extra_paragraphs = Read-U16 12
        initial_ss = Read-U16 14
        initial_sp = Read-U16 16
        initial_ip = Read-U16 20
        initial_cs = Read-U16 22
        overlay = Read-U16 26
    }
    disassembly = [ordered]@{
        tool = $ndisasm
        output = $assemblyPath
        line_count = $lines.Count
        call_instruction_count = $callLines.Count
        interrupt_instruction_count = $interruptLines.Count
        near_return_count = @($lines | Where-Object { $_ -match '\bret\b' }).Count
        far_return_count = @($lines | Where-Object { $_ -match '\bretf\b' }).Count
    }
    relocation_entries = $relocations
}

$summaryPath = Join-Path $OutputDirectory "$Prefix-audit.json"
[System.IO.File]::WriteAllText($summaryPath,
    ($summary | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))

[pscustomobject]@{
    input = $InputPath
    image = $imagePath
    disassembly = $assemblyPath
    strings = $stringsPath
    audit = $summaryPath
    image_bytes = $imageLength
    relocation_count = $relocationCount
    disassembly_lines = $lines.Count
    call_instructions = $callLines.Count
}
