[CmdletBinding()]
param(
    [string]$OutputPath,
    [string]$BaselineId,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$rustReferenceManifest = Join-Path $repoRoot "SparrowCore\Tests\RustReference\Cargo.toml"
$defaultOutput = Join-Path (Split-Path $PSScriptRoot -Parent) "baseline-2026-10-09.json"
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = $defaultOutput
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if ([string]::IsNullOrWhiteSpace($BaselineId)) {
    $BaselineId = if ($OutputPath -ieq [IO.Path]::GetFullPath($defaultOutput)) {
        "sparrow-parity-2026-10-09-pre-p0"
    } else {
        [IO.Path]::GetFileNameWithoutExtension($OutputPath)
    }
}
if ((Test-Path -LiteralPath $OutputPath) -and -not $Force) {
    throw "Refusing to replace frozen baseline '$OutputPath'. Use -Force only for an intentional re-freeze."
}

$excludedDirectoryPattern = '[\\/](\.git|target|build|build-[^\\/]+|debug|release|bin|obj|moc|rcc|\.obj|\.pch|node_modules|out)([\\/]|$)'
$sourceExtensions = @(
    ".rs", ".toml", ".lock", ".json", ".tsv", ".csv", ".md", ".txt",
    ".h", ".hh", ".hpp", ".cpp", ".cc", ".cxx", ".c", ".pro", ".pri",
    ".cmake", ".rc", ".xml", ".ps1", ".cmd", ".bat", ".sh", ".yaml",
    ".yml", ".svg", ".js", ".jsx", ".ts", ".tsx", ".html", ".css"
)

function Get-FileDigest([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TreeSnapshot([string]$Name, [string]$Root, [string]$PathPrefix) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "Source root '$Root' for '$Name' does not exist."
    }
    $resolvedRoot = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
    $files = Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Force |
        Where-Object {
            $_.FullName -notmatch $excludedDirectoryPattern -and
            $_.Name -notmatch '^baseline-\d{4}-\d{2}-\d{2}\.json$' -and
            ($sourceExtensions -contains $_.Extension.ToLowerInvariant() -or
             $_.FullName -match '[\\/]Fixtures[\\/]')
        } |
        Sort-Object FullName

    $records = [System.Collections.Generic.List[object]]::new()
    [long]$totalBytes = 0
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($resolvedRoot.Length).TrimStart('\', '/').Replace('\', '/')
        $totalBytes += [long]$file.Length
        $records.Add([ordered]@{
            path = "$PathPrefix/$relative"
            bytes = [long]$file.Length
            sha256 = Get-FileDigest $file.FullName
        })
    }
    $digestInput = ($records | ForEach-Object { $_.path + [char]9 + $_.sha256 }) -join [Environment]::NewLine
    $digestBytes = [Text.UTF8Encoding]::new($false).GetBytes($digestInput)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $rootDigest = ([BitConverter]::ToString($sha.ComputeHash($digestBytes))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
    return [ordered]@{
        name = $Name
        root = $resolvedRoot
        fileCount = $records.Count
        totalBytes = $totalBytes
        sha256 = $rootDigest
        files = @($records)
    }
}

$metadataOutput = & cargo metadata --locked --manifest-path $rustReferenceManifest --format-version 1 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "cargo metadata failed with exit code $LASTEXITCODE."
}
$metadata = ($metadataOutput -join [Environment]::NewLine) | ConvertFrom-Json
$jaguaPackage = $metadata.packages |
    Where-Object { $_.name -eq "jagua-rs" -and $_.source -like "git+*" } |
    Select-Object -First 1
if ($null -eq $jaguaPackage) {
    throw "Cargo metadata did not resolve the locked jagua-rs git package."
}
$resolvedJaguaPackageRoot = Split-Path $jaguaPackage.manifest_path -Parent
$resolvedJaguaCheckoutRoot = Split-Path $resolvedJaguaPackageRoot -Parent

$sourceRoots = @(
    Get-TreeSnapshot "Sparrow Rust working source" (Join-Path $repoRoot "CodeResource\sparrow") "repo://CodeResource/sparrow"
    Get-TreeSnapshot "jagua-rs local mirror" (Join-Path $repoRoot "CodeResource\jagua-rs") "repo://CodeResource/jagua-rs"
    Get-TreeSnapshot "jagua-rs Cargo-resolved checkout" $resolvedJaguaCheckoutRoot "cargo://jagua-rs"
    Get-TreeSnapshot "JaguarCore C++ source and tests" (Join-Path $repoRoot "JaguarCore") "repo://JaguarCore"
    Get-TreeSnapshot "SparrowCore C++ source, tests, and tools" (Join-Path $repoRoot "SparrowCore") "repo://SparrowCore"
    Get-TreeSnapshot "Sparrow comparison records" (Join-Path $repoRoot "docs\logic-comparison-sparrow") "repo://docs/logic-comparison-sparrow"
)

$cargoLockPath = Join-Path $repoRoot "SparrowCore\Tests\RustReference\Cargo.lock"
$cargoLockHash = Get-FileDigest $cargoLockPath
$lockText = Get-Content -LiteralPath $cargoLockPath -Raw
$jaguaLockMatch = [regex]::Match(
    $lockText,
    '(?ms)\[\[package\]\]\s*name = "jagua-rs"\s*version = "[^"]+"\s*source = "[^"]+#(?<revision>[0-9a-f]{40})"')
if (-not $jaguaLockMatch.Success) {
    throw "Could not read a 40-character jagua-rs revision from Cargo.lock."
}

$rustcVersion = (& rustc --version --verbose 2>&1) -join [Environment]::NewLine
$cargoVersion = (& cargo --version --verbose 2>&1) -join [Environment]::NewLine
$featureTree = (& cargo tree --locked --manifest-path $rustReferenceManifest --target x86_64-pc-windows-msvc -e features -i jagua-rs 2>&1) -join [Environment]::NewLine
if ($LASTEXITCODE -ne 0) {
    throw "cargo tree failed with exit code $LASTEXITCODE."
}

$testMakefile = Join-Path $repoRoot "SparrowCore\build\tests-rust-equivalence-debug\Makefile"
$qmakeLine = Select-String -LiteralPath $testMakefile -Pattern '^QMAKE\s*=\s*(.+)$' |
    Select-Object -First 1
$qmakePath = if ($qmakeLine) { $qmakeLine.Matches[0].Groups[1].Value.Trim() } else { $null }
$qmakeVersion = if ($qmakePath -and (Test-Path -LiteralPath $qmakePath)) {
    (& $qmakePath --version 2>&1) -join [Environment]::NewLine
} else {
    "unavailable from the generated test Makefile"
}
$compilerInPath = Get-Command cl.exe -ErrorAction SilentlyContinue
$nmakeInPath = Get-Command nmake.exe -ErrorAction SilentlyContinue
$programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
$vswhereCandidates = @(
    (Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe")
    (Join-Path ([Environment]::GetEnvironmentVariable("ProgramFiles")) "Microsoft Visual Studio\Installer\vswhere.exe")
)
$vswhere = $vswhereCandidates |
    Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
    Select-Object -First 1
$vsInstallPath = if ($vswhere) { (& $vswhere -latest -products "*" -property installationPath | Select-Object -First 1) } else { $null }
$toolsetRoot = $null
if ($vsInstallPath) {
    $toolsetRoot = Get-ChildItem -LiteralPath (Join-Path $vsInstallPath "VC\Tools\MSVC") -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
$toolsetCompilerPath = if ($toolsetRoot) { Join-Path $toolsetRoot "bin\Hostx64\x64\cl.exe" } else { $null }
$toolsetNmakePath = if ($toolsetRoot) { Join-Path $toolsetRoot "bin\Hostx64\x64\nmake.exe" } else { $null }
$compilerPath = if ($compilerInPath) { $compilerInPath.Source } elseif ($toolsetCompilerPath -and (Test-Path -LiteralPath $toolsetCompilerPath)) { $toolsetCompilerPath } else { $null }
$nmakePath = if ($nmakeInPath) { $nmakeInPath.Source } elseif ($toolsetNmakePath -and (Test-Path -LiteralPath $toolsetNmakePath)) { $toolsetNmakePath } else { $null }
$compilerFileVersion = if ($compilerPath) { (Get-Item -LiteralPath $compilerPath).VersionInfo.FileVersion } else { $null }
$nmakeFileVersion = if ($nmakePath) { (Get-Item -LiteralPath $nmakePath).VersionInfo.FileVersion } else { $null }

$artifactCandidates = @(
    "SparrowCore\build\build_Kit_MSVC_2019_x64-Debug\Makefile"
    "SparrowCore\build\build_Kit_MSVC_2019_x64-Release\Makefile"
    "SparrowCore\build\tests-rust-equivalence-debug\Makefile"
    "SparrowCore\build\tests-rust-equivalence-release\Makefile"
    "SparrowCore\build\tests-rust-equivalence-debug\debug\SparrowCoreTests.exe"
    "SparrowCore\build\tests-rust-equivalence-release\release\SparrowCoreTests.exe"
    "SparrowCore\build\build_Kit_MSVC_2019_x64-Debug\bin\SparrowCore.lib"
    "SparrowCore\build\build_Kit_MSVC_2019_x64-Release\bin\SparrowCore.lib"
    "JaguarCore\build\build_Kit_MSVC_2019_x64-Debug\Bin\JaguarCored.lib"
    "JaguarCore\build\build_Kit_MSVC_2019_x64-Release\bin\JaguarCore.lib"
    "JaguarCore\build\build_Kit_MSVC_2019_x64-Debug\Bin\JaguarCored.dll"
    "JaguarCore\build\build_Kit_MSVC_2019_x64-Release\bin\JaguarCore.dll"
)
$buildArtifacts = [System.Collections.Generic.List[object]]::new()
foreach ($relativePath in $artifactCandidates) {
    $fullPath = Join-Path $repoRoot $relativePath
    if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
        $file = Get-Item -LiteralPath $fullPath
        $buildArtifacts.Add([ordered]@{
            path = "repo://$($relativePath.Replace('\', '/'))"
            bytes = [long]$file.Length
            lastWriteTimeUtc = $file.LastWriteTimeUtc.ToString("o")
            sha256 = Get-FileDigest $fullPath
        })
    }
}

$manifest = [ordered]@{
    schemaVersion = 1
    baselineId = $BaselineId
    frozenAtUtc = [DateTime]::UtcNow.ToString("o")
    workspace = $repoRoot
    method = "Per-file SHA-256 and deterministic sorted-tree digest; no VCS metadata used."
    rust = [ordered]@{
        toolchainFile = "repo://SparrowCore/Tests/RustReference/rust-toolchain.toml"
        rustc = $rustcVersion
        cargo = $cargoVersion
        target = "x86_64-pc-windows-msvc"
        cargoLock = [ordered]@{ path = "repo://SparrowCore/Tests/RustReference/Cargo.lock"; sha256 = $cargoLockHash }
        jaguaRsRevision = $jaguaLockMatch.Groups["revision"].Value
        jaguaRsResolvedPackageRoot = $resolvedJaguaPackageRoot
        effectiveJaguaRsFeatures = $featureTree
    }
    cpp = [ordered]@{
        languageStandard = "C++17, from SparrowCore and JaguarCore qmake projects"
        qmake = $qmakePath
        qmakeVersion = $qmakeVersion
        qmakeSpec = "win32-msvc; generated build directories identify the Qt 6.5.3 msvc2019_64 kit"
        msvcInstallation = $vsInstallPath
        msvcToolsetRoot = $toolsetRoot
        compilerPath = $compilerPath
        compilerFileVersion = $compilerFileVersion
        nmakePath = $nmakePath
        nmakeFileVersion = $nmakeFileVersion
        compilerStatus = if ($compilerInPath) { "cl.exe is available in the invoking environment" } elseif ($compilerPath) { "the latest installed x64 MSVC compiler is recorded; cl.exe is not on the invoking PowerShell PATH" } else { "MSVC compiler path could not be resolved; generated build configuration and built artifacts are fingerprinted separately" }
        featureNotes = "JaguarCore project enables its AVX2 source unless jaguarcore_no_avx2 is set; Sparrow RustReference has oracle-trace and jagua-rs default+spp, with Sparrow simd disabled."
    }
    buildArtifacts = @($buildArtifacts)
    sourceRoots = @($sourceRoots)
}

$json = $manifest | ConvertTo-Json -Depth 12
$outputDirectory = Split-Path $OutputPath -Parent
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
[IO.File]::WriteAllText($OutputPath, $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output "Frozen baseline: $OutputPath"
Write-Output "jagua-rs revision: $($jaguaLockMatch.Groups['revision'].Value)"
foreach ($root in $sourceRoots) {
    Write-Output "$($root.name): files=$($root.fileCount) sha256=$($root.sha256)"
}
Write-Output "Build artifacts fingerprinted: $($buildArtifacts.Count)"
