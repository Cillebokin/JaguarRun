[CmdletBinding()]
param(
    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return [System.IO.Path]::GetFullPath($Path)
}

function Relative-Path {
    param(
        [Parameter(Mandatory = $true)][string]$BasePath,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $base = (Resolve-FullPath $BasePath).TrimEnd('\') + '\'
    $full = Resolve-FullPath $Path
    return $full.Substring($base.Length).Replace('\', '/')
}

$repoRootPath = Resolve-FullPath $RepoRoot
$coreRoot = Join-Path $repoRootPath 'JaguarCore'
$includeRoot = Join-Path $coreRoot 'Include'
$sourceRoot = Join-Path $coreRoot 'Source'
$testsRoot = Join-Path $coreRoot 'Tests'
$packageRoot = Join-Path $coreRoot 'Package'

foreach ($requiredPath in @($coreRoot, $includeRoot, $sourceRoot, $testsRoot, $packageRoot)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Container)) {
        throw "Required JaguarCore directory is missing: $requiredPath"
    }
}

$publicHeaders = @(Get-ChildItem -LiteralPath (Join-Path $includeRoot 'JaguarCore') -Recurse -File -Filter '*.h')
if ($publicHeaders.Count -ne 31) {
    throw "Expected 31 public headers, found $($publicHeaders.Count)"
}

$sourceScanRoots = @($includeRoot, $sourceRoot, $testsRoot, $packageRoot)
$sourceExtensions = @('.h', '.cpp', '.pro', '.ps1')
$nonConformingFiles = @(
    foreach ($scanRoot in $sourceScanRoots) {
        foreach ($file in Get-ChildItem -LiteralPath $scanRoot -Recurse -File) {
            if ((($sourceExtensions -contains $file.Extension.ToLowerInvariant()) -and
                 ($file.Name -cnotmatch '^[A-Z][A-Za-z0-9_.-]*$'))) {
                [pscustomobject]@{
                    Path = Relative-Path $repoRootPath $file.FullName
                    Name = $file.Name
                }
            }
        }
    }
)
if ($nonConformingFiles.Count -gt 0) {
    $paths = ($nonConformingFiles | ForEach-Object { $_.Path }) -join ', '
    throw "Code file names must begin with an uppercase letter: $paths"
}

$nonConformingDirectories = @(
    foreach ($scanRoot in $sourceScanRoots) {
        foreach ($directory in Get-ChildItem -LiteralPath $scanRoot -Recurse -Directory) {
            if ($directory.Name -cnotmatch '^[A-Z][A-Za-z0-9_-]*$') {
                [pscustomobject]@{
                    Path = Relative-Path $repoRootPath $directory.FullName
                    Name = $directory.Name
                }
            }
        }
    }
)
if ($nonConformingDirectories.Count -gt 0) {
    $paths = ($nonConformingDirectories | ForEach-Object { $_.Path }) -join ', '
    throw "Code directory names must begin with an uppercase letter: $paths"
}

$lowercaseApiPattern = '\b[a-z][A-Za-z0-9_]*\s*\('
$ignoredLowercasePattern = 'std::|isfinite|abs|max|min|move|operator|\b(if|for|while|switch|return)\b|m_(Error|Value)|irrelevant(Handle)?'
$lowercaseApiMatches = @(
    foreach ($header in $publicHeaders) {
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $header.FullName) {
            $lineNumber++
            if ($line -cmatch $lowercaseApiPattern -and $line -cnotmatch $ignoredLowercasePattern) {
                [pscustomobject]@{
                    Path = Relative-Path $repoRootPath $header.FullName
                    Line = $lineNumber
                    Text = $line.Trim()
                }
            }
        }
    }
)
if ($lowercaseApiMatches.Count -gt 0) {
    $details = ($lowercaseApiMatches | ForEach-Object {
        "$($_.Path):$($_.Line): $($_.Text)"
    }) -join [Environment]::NewLine
    throw "Lowercase public API declarations were found:`n$details"
}

$bannedConstructPattern = '\b(try|catch|using\s+namespace|Q_OBJECT)\b|\bconnect\s*\('
$bannedConstructMatches = @(
    foreach ($scanRoot in $sourceScanRoots) {
        Get-ChildItem -LiteralPath $scanRoot -Recurse -File |
            Where-Object { $_.Extension.ToLowerInvariant() -in @('.h', '.cpp') } |
            Select-String -Pattern $bannedConstructPattern
    }
)
if ($bannedConstructMatches.Count -gt 0) {
    $details = ($bannedConstructMatches | ForEach-Object {
        "$($_.Path):$($_.LineNumber): $($_.Line.Trim())"
    }) -join [Environment]::NewLine
    throw "Disallowed constructs were found:`n$details"
}

Write-Output "source_contract,public_headers,$($publicHeaders.Count)"
Write-Output 'source_contract,api_names,pass'
Write-Output 'source_contract,result,pass'
