[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$PackageRoot,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$ExpectedPublicVersion,

    [Parameter(Mandatory = $true, Position = 2)]
    [string]$ExpectedFileVersion,

    [Parameter(Mandatory = $true, Position = 3)]
    [string]$ExpectedApiVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$invalidApiVersion = [string]::IsNullOrWhiteSpace($ExpectedApiVersion) -or ($ExpectedApiVersion -notmatch '^[0-9]+$')
if ($invalidApiVersion) {
    throw 'ExpectedApiVersion must be a non-negative decimal integer'
}

try {
    $apiVersionValue = [uint64]$ExpectedApiVersion
} catch {
    throw 'ExpectedApiVersion must be a non-negative decimal integer'
}
if ($apiVersionValue -gt [uint32]::MaxValue) {
    throw 'ExpectedApiVersion must fit in uint32'
}

$resolvedRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
$includeRoot = Join-Path $resolvedRoot 'Include\JaguarCore'
$requiredSharedFiles = @(
    (Join-Path $resolvedRoot 'README.md'),
    (Join-Path $resolvedRoot 'Docs\deployment.md'),
    (Join-Path $resolvedRoot 'Docs\jaguarcore-api-v2-migration.md'),
    (Join-Path $includeRoot 'Version.h')
)

foreach ($path in $requiredSharedFiles) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing package file: $path"
    }
}

$headers = @(Get-ChildItem -LiteralPath $includeRoot -Recurse -File -Filter '*.h')
if ($headers.Count -ne 31) {
    throw "Expected 31 public headers, found $($headers.Count)"
}

$nonConformingHeaders = @(
    $headers | Where-Object { $_.Name -notmatch '^[A-Z][A-Za-z0-9_.-]*\.h$' }
)
if ($nonConformingHeaders.Count -gt 0) {
    $paths = ($nonConformingHeaders | ForEach-Object { $_.FullName }) -join ', '
    throw "Package contains non-conforming public header names: $paths"
}

$foreignRuntimePattern = '^(Qt[56].*\.dll|qwindows\.dll|libstdc\+\+.*\.(dll|lib)|libgcc.*\.(dll|lib)|libwinpthread.*\.dll|rust.*\.(dll|lib|exe))$'
$foreignRuntimeFiles = @(
    Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File |
        Where-Object { $_.Name -match $foreignRuntimePattern }
)
if ($foreignRuntimeFiles.Count -gt 0) {
    $paths = ($foreignRuntimeFiles | ForEach-Object { $_.FullName }) -join ', '
    throw "Package contains forbidden Qt/Rust runtime files: $paths"
}

$cases = @(
    @{
        Name = 'Debug/AVX2'
        Configuration = 'Debug'
        Checker = 'Debug\Tools\JaguarCoreRuntimeCheck.exe'
        Dll = 'Debug\AVX2\Bin\JaguarCored.dll'
        Lib = 'Debug\AVX2\Lib\JaguarCored.lib'
    },
    @{
        Name = 'Debug/Scalar'
        Configuration = 'Debug'
        Checker = 'Debug\Tools\JaguarCoreRuntimeCheck.exe'
        Dll = 'Debug\Scalar\Bin\JaguarCored.dll'
        Lib = 'Debug\Scalar\Lib\JaguarCored.lib'
    },
    @{
        Name = 'Release/AVX2'
        Configuration = 'Release'
        Checker = 'Release\Tools\JaguarCoreRuntimeCheck.exe'
        Dll = 'Release\AVX2\Bin\JaguarCore.dll'
        Lib = 'Release\AVX2\Lib\JaguarCore.lib'
    },
    @{
        Name = 'Release/Scalar'
        Configuration = 'Release'
        Checker = 'Release\Tools\JaguarCoreRuntimeCheck.exe'
        Dll = 'Release\Scalar\Bin\JaguarCore.dll'
        Lib = 'Release\Scalar\Lib\JaguarCore.lib'
    }
)

foreach ($case in $cases) {
    $checker = Join-Path $resolvedRoot $case.Checker
    $dll = Join-Path $resolvedRoot $case.Dll
    $lib = Join-Path $resolvedRoot $case.Lib

    foreach ($path in @($checker, $dll, $lib)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Missing $($case.Name) package file: $path"
        }
    }

    & $checker $dll $ExpectedPublicVersion $ExpectedFileVersion $ExpectedApiVersion $case.Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Runtime validation failed for $($case.Name) with exit code $LASTEXITCODE"
    }
}

Write-Output 'package_check,result,pass'
