param(
    [string]$AssetDirectory = (Join-Path $PSScriptRoot "..\assets"),
    [string]$OutputPath = (Join-Path $PSScriptRoot "..\assets\ASSET-MANIFEST.json")
)

$ErrorActionPreference = "Stop"
$AssetDirectory = (Resolve-Path $AssetDirectory).Path
$records = Get-ChildItem $AssetDirectory -Recurse -File |
    Where-Object { $_.FullName -ne [System.IO.Path]::GetFullPath($OutputPath) } |
    Sort-Object FullName |
    ForEach-Object {
        [pscustomobject][ordered]@{
            path = $_.FullName.Substring($AssetDirectory.Length + 1).Replace('\', '/')
            bytes = $_.Length
            sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
        }
    }

$manifest = [ordered]@{
    format_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    asset_count = @($records).Count
    total_bytes = [int64](($records | Measure-Object bytes -Sum).Sum)
    files = @($records)
}

[System.IO.File]::WriteAllText(
    [System.IO.Path]::GetFullPath($OutputPath),
    ($manifest | ConvertTo-Json -Depth 4),
    [Text.UTF8Encoding]::new($false))

[pscustomobject]@{
    output = [System.IO.Path]::GetFullPath($OutputPath)
    assets = $manifest.asset_count
    bytes = $manifest.total_bytes
}
