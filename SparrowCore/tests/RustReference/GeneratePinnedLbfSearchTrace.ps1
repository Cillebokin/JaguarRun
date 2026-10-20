$ErrorActionPreference = 'Stop'

$revision = 'f5e2b5ffd14931f17a20ce283a2122e877953ceb'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$oracleManifest = Join-Path $repoRoot 'JaguarCore\Tools\RustOracle\Cargo.toml'
$recordedLock = Join-Path $repoRoot 'JaguarCore\Tools\RustOracle\LbfFixture.Cargo.lock'
$helperSource = Join-Path $PSScriptRoot 'pinned_lbf_search_trace.rs'
$outputPath = Join-Path $repoRoot 'SparrowCore\Tests\Fixtures\LbfPinnedSearchRustTrace.tsv'
$normalOutputPath = Join-Path $repoRoot 'SparrowCore\Tests\Fixtures\LbfPinnedNormalRustTrace.tsv'
$cargoHome = if ([string]::IsNullOrWhiteSpace($env:CARGO_HOME)) {
    Join-Path $env:USERPROFILE '.cargo'
} else {
    $env:CARGO_HOME
}
$checkoutRoot = Join-Path $cargoHome 'git\checkouts'
$revisionPrefix = $revision.Substring(0, 7)

$matches = @()
if (Test-Path -LiteralPath $checkoutRoot) {
    foreach ($repositoryCache in Get-ChildItem -LiteralPath $checkoutRoot -Directory) {
        $matches += Get-ChildItem -LiteralPath $repositoryCache.FullName -Directory |
            Where-Object { $_.Name -eq $revisionPrefix }
    }
}

if ($matches.Count -eq 0) {
    Push-Location $repoRoot
    try {
        & rustup run 1.98.1 cargo fetch --locked --manifest-path $oracleManifest
        if ($LASTEXITCODE -ne 0) {
            throw 'Could not fetch the E4-pinned jagua-rs checkout'
        }
    } finally {
        Pop-Location
    }
    foreach ($repositoryCache in Get-ChildItem -LiteralPath $checkoutRoot -Directory) {
        $matches += Get-ChildItem -LiteralPath $repositoryCache.FullName -Directory |
            Where-Object { $_.Name -eq $revisionPrefix }
    }
}

if ($matches.Count -ne 1) {
    throw "Expected one Cargo checkout for jagua-rs $revision; found $($matches.Count)"
}

$pinnedRoot = $matches[0].FullName
$workspaceLock = Join-Path $pinnedRoot 'Cargo.lock'
$recordedLockHash = (Get-FileHash -LiteralPath $recordedLock -Algorithm SHA256).Hash
if (-not (Test-Path -LiteralPath $workspaceLock)) {
    throw "The pinned checkout has no Cargo.lock: $workspaceLock"
}
$workspaceLockHash = (Get-FileHash -LiteralPath $workspaceLock -Algorithm SHA256).Hash
if ($workspaceLockHash -ne $recordedLockHash) {
    throw "The cached Cargo.lock differs from the recorded LBF fixture lock; review before tracing: $workspaceLock"
}

$binaryDirectory = Join-Path $pinnedRoot 'lbf\src\bin'
$temporaryBinary = Join-Path $binaryDirectory 'jaguar_run_lbf_search_normal_trace.rs'
$manifest = Join-Path $pinnedRoot 'lbf\Cargo.toml'
$createdBinaryDirectory = -not (Test-Path -LiteralPath $binaryDirectory)
if (Test-Path -LiteralPath $temporaryBinary) {
    throw "Refusing to overwrite an existing diagnostic binary: $temporaryBinary"
}

if ($createdBinaryDirectory) {
    New-Item -ItemType Directory -Path $binaryDirectory | Out-Null
}

try {
    Copy-Item -LiteralPath $helperSource -Destination $temporaryBinary
    Push-Location $repoRoot
    try {
        & rustup run 1.98.1 cargo run --locked --manifest-path $manifest `
            --bin jaguar_run_lbf_search_normal_trace -- $outputPath $normalOutputPath
        if ($LASTEXITCODE -ne 0) {
            throw 'Pinned LBF candidate trace generation failed'
        }
    } finally {
        Pop-Location
    }
} finally {
    if (Test-Path -LiteralPath $temporaryBinary) {
        Remove-Item -LiteralPath $temporaryBinary -Force
    }
    if ($createdBinaryDirectory -and (Test-Path -LiteralPath $binaryDirectory)) {
        $remaining = Get-ChildItem -LiteralPath $binaryDirectory -Force
        if ($remaining.Count -eq 0) {
            Remove-Item -LiteralPath $binaryDirectory
        }
    }
}

Write-Host "Generated pinned LBF candidate trace from jagua-rs $revision"
