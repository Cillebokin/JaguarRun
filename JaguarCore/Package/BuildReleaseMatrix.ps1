[CmdletBinding()]
param(
    [string]$QtBin = 'D:\MySoftwares\Tools\Qt\Qt5.12.10\5.12.10\msvc2017_64\bin',
    [string]$VcVars = 'D:\MySoftwares\Tools\VisualStudio\VS2019\Professional\VC\Auxiliary\Build\vcvars64.bat',
    [string]$BuildRoot = (Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))) 'build-JaguarCore-matrix'),
    [string]$PackageRoot = 'D:\MyWorks\ProQt\package-JaguarCore-0.1.0-msvc2019-x64',
    [string]$ExpectedPublicVersion = '0.1.0-dev',
    [string]$ExpectedFileVersion = '0.1.0.0',
    [int]$ExpectedApiVersion = 2,
    [switch]$RequireEmptyPackageRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return [System.IO.Path]::GetFullPath($Path)
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    Push-Location -LiteralPath $WorkingDirectory
    try {
        & $FilePath @Arguments
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    if ($exitCode -ne 0) {
        throw "Command failed with exit code ${exitCode}: $FilePath $($Arguments -join ' ')"
    }
}

function Invoke-CheckedWithPath {
    param(
        [Parameter(Mandatory = $true)][string]$PathPrefix,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory
    )

    $originalPath = $env:Path
    try {
        $env:Path = "$PathPrefix;$originalPath"
        Invoke-Checked -FilePath $FilePath -Arguments $Arguments -WorkingDirectory $WorkingDirectory
    } finally {
        $env:Path = $originalPath
    }
}

function Import-VcEnvironment {
    param([Parameter(Mandatory = $true)][string]$BatchFile)

    $command = 'call "' + $BatchFile + '" >nul && set'
    $environmentLines = @(& cmd.exe /d /s /c $command)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to import Visual Studio environment from $BatchFile"
    }

    foreach ($line in $environmentLines) {
        $separator = $line.IndexOf('=')
        if ($separator -le 0) {
            continue
        }

        $name = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 1)
        [System.Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}

function New-IsolatedRunRoot {
    param([Parameter(Mandatory = $true)][string]$Root)

    New-Item -ItemType Directory -Force -Path $Root | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $candidate = Join-Path $Root "run-$stamp"
    $suffix = 0
    while (Test-Path -LiteralPath $candidate) {
        $suffix++
        $candidate = Join-Path $Root "run-$stamp-$suffix"
    }
    New-Item -ItemType Directory -Path $candidate | Out-Null
    return $candidate
}

$repoRoot = Resolve-FullPath (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$sourceRoot = Join-Path $repoRoot 'JaguarCore'
$qtBinPath = Resolve-FullPath $QtBin
$vcVarsPath = Resolve-FullPath $VcVars
$buildRootPath = Resolve-FullPath $BuildRoot
$packageRootPath = Resolve-FullPath $PackageRoot

if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) {
    throw "JaguarCore source directory does not exist: $sourceRoot"
}
if (-not (Test-Path -LiteralPath $qtBinPath -PathType Container)) {
    throw "Qt binary directory does not exist: $qtBinPath"
}
if (-not (Test-Path -LiteralPath $vcVarsPath -PathType Leaf)) {
    throw "Visual Studio environment script does not exist: $vcVarsPath"
}
if ($buildRootPath -match '(?i)(^|[\\/])(?:debug|release)(?:[-_\\/]|$)') {
    throw "BuildRoot must not contain a debug/release path token because Qt qmake rewrites shadow-build output paths: $buildRootPath"
}

if (-not (Get-Command nmake.exe -ErrorAction SilentlyContinue)) {
    Import-VcEnvironment $vcVarsPath
}

$qmake = Join-Path $qtBinPath 'qmake.exe'
if (-not (Test-Path -LiteralPath $qmake -PathType Leaf)) {
    throw "qmake.exe does not exist: $qmake"
}
if (-not (Get-Command nmake.exe -ErrorAction SilentlyContinue)) {
    throw 'nmake.exe is not available after importing the Visual Studio environment'
}

$sourceContractScript = Join-Path $sourceRoot 'Package\VerifySourceContract.ps1'
if (-not (Test-Path -LiteralPath $sourceContractScript -PathType Leaf)) {
    throw "Source contract verifier does not exist: $sourceContractScript"
}
Invoke-Checked 'powershell.exe' @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $sourceContractScript,
    '-RepoRoot', $repoRoot
) $repoRoot

if ($RequireEmptyPackageRoot -and (Test-Path -LiteralPath $packageRootPath)) {
    $existingPackageEntries = @(Get-ChildItem -LiteralPath $packageRootPath -Force)
    if ($existingPackageEntries.Count -gt 0) {
        throw "PackageRoot must be empty when -RequireEmptyPackageRoot is used: $packageRootPath"
    }
}

$runRoot = New-IsolatedRunRoot $buildRootPath
New-Item -ItemType Directory -Force -Path $packageRootPath | Out-Null

$coreProject = Join-Path $sourceRoot 'JaguarCore.pro'
$testsProject = Join-Path $sourceRoot 'Tests\JaguarCoreTests.pro'
$runtimeProject = Join-Path $sourceRoot 'Package\JaguarCoreRuntimeCheck.pro'
$consumerProject = Join-Path $sourceRoot 'Package\JaguarCoreConsumerSmoke.pro'

$matrix = @(
    [pscustomobject]@{ Name = 'Debug/AVX2'; Configuration = 'Debug'; QmakeConfiguration = 'debug'; DirectoryToken = 'cfg0'; Variant = 'AVX2'; IsScalar = $false },
    [pscustomobject]@{ Name = 'Debug/Scalar'; Configuration = 'Debug'; QmakeConfiguration = 'debug'; DirectoryToken = 'cfg0'; Variant = 'Scalar'; IsScalar = $true },
    [pscustomobject]@{ Name = 'Release/AVX2'; Configuration = 'Release'; QmakeConfiguration = 'release'; DirectoryToken = 'cfg1'; Variant = 'AVX2'; IsScalar = $false },
    [pscustomobject]@{ Name = 'Release/Scalar'; Configuration = 'Release'; QmakeConfiguration = 'release'; DirectoryToken = 'cfg1'; Variant = 'Scalar'; IsScalar = $true }
)

$results = @()
foreach ($tuple in $matrix) {
    $coreBuild = Join-Path $runRoot "core-$($tuple.DirectoryToken)-$($tuple.Variant)"
    $testBuild = Join-Path $runRoot "tests-$($tuple.DirectoryToken)-$($tuple.Variant)"
    New-Item -ItemType Directory -Force -Path $coreBuild, $testBuild | Out-Null

    $coreArguments = @($coreProject, "CONFIG+=$($tuple.QmakeConfiguration)")
    if ($tuple.IsScalar) {
        $coreArguments += 'CONFIG+=jaguarcore_no_avx2'
    }
    $coreArguments += "JAGUARCORE_PACKAGE_ROOT=$packageRootPath"
    Invoke-Checked $qmake $coreArguments $coreBuild
    Invoke-Checked 'nmake.exe' @('/f', "Makefile.$($tuple.Configuration)", '/NOLOGO', 'install') $coreBuild

    $testArguments = @($testsProject, "CONFIG+=$($tuple.QmakeConfiguration)")
    if ($tuple.IsScalar) {
        $testArguments += 'CONFIG+=jaguarcore_no_avx2'
    }
    $testArguments += "JAGUARCORE_BUILD_DIR=$coreBuild"
    Invoke-Checked $qmake $testArguments $testBuild
    Invoke-Checked 'nmake.exe' @('/f', "Makefile.$($tuple.Configuration)", '/NOLOGO') $testBuild

    $testExecutable = Join-Path $testBuild ("$($tuple.QmakeConfiguration)\JaguarCoreTests.exe")
    if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
        throw "Native test executable was not produced: $testExecutable"
    }
    # The native suite intentionally resolves legacy Rust-oracle fixtures from
    # the repository root; keep the DLL search path isolated while providing
    # that stable data working directory.
    Invoke-CheckedWithPath (Join-Path $coreBuild 'Bin') $testExecutable @() $repoRoot

    $results += [pscustomobject]@{
        Name = $tuple.Name
        Build = 'pass'
        NativeTests = 'pass'
        Consumer = 'pending'
        RuntimeCheck = 'pending'
    }
}

$runtimeConfigurations = @(
    [pscustomobject]@{ Configuration = 'Debug'; QmakeConfiguration = 'debug'; DirectoryToken = 'cfg0' },
    [pscustomobject]@{ Configuration = 'Release'; QmakeConfiguration = 'release'; DirectoryToken = 'cfg1' }
)
foreach ($configuration in $runtimeConfigurations) {
    $checkerBuild = Join-Path $runRoot "runtime-check-$($configuration.DirectoryToken)"
    New-Item -ItemType Directory -Force -Path $checkerBuild | Out-Null
    $checkerArguments = @(
        $runtimeProject,
        "CONFIG+=$($configuration.QmakeConfiguration)",
        "JAGUARCORE_PACKAGE_ROOT=$packageRootPath"
    )
    Invoke-Checked $qmake $checkerArguments $checkerBuild
    Invoke-Checked 'nmake.exe' @('/f', "Makefile.$($configuration.Configuration)", '/NOLOGO', 'install') $checkerBuild
}

foreach ($tuple in $matrix) {
    $consumerBuild = Join-Path $runRoot "consumer-$($tuple.DirectoryToken)-$($tuple.Variant)"
    New-Item -ItemType Directory -Force -Path $consumerBuild | Out-Null
    $consumerArguments = @(
        $consumerProject,
        "CONFIG+=$($tuple.QmakeConfiguration)",
        "JAGUARCORE_PACKAGE_ROOT=$packageRootPath",
        "JAGUARCORE_PACKAGE_CONFIGURATION=$($tuple.Configuration)",
        "JAGUARCORE_PACKAGE_VARIANT=$($tuple.Variant)"
    )
    Invoke-Checked $qmake $consumerArguments $consumerBuild
    Invoke-Checked 'nmake.exe' @('/f', "Makefile.$($tuple.Configuration)", '/NOLOGO') $consumerBuild

    $consumerExecutable = Join-Path $consumerBuild ("Bin\JaguarCoreConsumerSmoke.exe")
    if (-not (Test-Path -LiteralPath $consumerExecutable -PathType Leaf)) {
        throw "Consumer executable was not produced: $consumerExecutable"
    }
    $packageBin = Join-Path $packageRootPath "$($tuple.Configuration)\$($tuple.Variant)\Bin"
    Invoke-CheckedWithPath $packageBin $consumerExecutable @() $consumerBuild
    for ($resultIndex = 0; $resultIndex -lt $results.Count; $resultIndex++) {
        if ($results[$resultIndex].Name -eq $tuple.Name) {
            $results[$resultIndex].Consumer = 'pass'
            break
        }
    }
}

$verifyScript = Join-Path $packageRootPath 'VerifyPackage.ps1'
if (-not (Test-Path -LiteralPath $verifyScript -PathType Leaf)) {
    throw "Installed package verifier was not produced: $verifyScript"
}
Invoke-Checked 'powershell.exe' @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $verifyScript,
    $packageRootPath,
    $ExpectedPublicVersion,
    $ExpectedFileVersion,
    $ExpectedApiVersion.ToString()
) $repoRoot

foreach ($row in $results) {
    $row.RuntimeCheck = 'pass'
}

$results | Format-Table -AutoSize | Out-String | Write-Output
Write-Output 'release_matrix,result,pass'
