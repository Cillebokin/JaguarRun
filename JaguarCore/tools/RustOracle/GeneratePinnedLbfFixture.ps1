$ErrorActionPreference = 'Stop'

$revision = 'f5e2b5ffd14931f17a20ce283a2122e877953ceb'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$rustOracleManifest = Join-Path $PSScriptRoot 'Cargo.toml'
$recordedLock = Join-Path $PSScriptRoot 'LbfFixture.Cargo.lock'
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
        & rustup run 1.98.1 cargo fetch --locked --manifest-path $rustOracleManifest
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
if (Test-Path -LiteralPath $workspaceLock) {
    $workspaceLockHash = (Get-FileHash -LiteralPath $workspaceLock -Algorithm SHA256).Hash
    if ($workspaceLockHash -ne $recordedLockHash) {
        throw "The cached Cargo.lock differs from the recorded LBF fixture lock; review before replacing it: $workspaceLock"
    }
} else {
    Copy-Item -LiteralPath $recordedLock -Destination $workspaceLock
}

$fixtureRoot = Join-Path $repoRoot 'JaguarCore\Tests\Fixtures\RustReference'
$inputFile = Join-Path $fixtureRoot 'optimizer_spp_instance.json'
$bppInputFile = Join-Path $fixtureRoot 'bpp_instance.json'
$configFile = Join-Path $fixtureRoot 'optimizer_lbf_config.json'
$solutionFolder = Join-Path ([IO.Path]::GetTempPath()) ('JaguarRun-LbfPinned-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $solutionFolder | Out-Null

Push-Location $repoRoot
try {
    & rustup run 1.98.1 cargo run --locked --manifest-path (Join-Path $pinnedRoot 'lbf\Cargo.toml') -- `
        --input-file $inputFile `
        --solution-folder $solutionFolder `
        --config-file $configFile `
        --prob-var spp `
        --log-level info
    if ($LASTEXITCODE -ne 0) {
        throw 'Pinned LBF fixture generation failed'
    }
} finally {
    Pop-Location
}

$fixtureOutput = Join-Path $fixtureRoot 'LbfSpp'
foreach ($extension in @('json', 'svg')) {
    $generated = Join-Path $solutionFolder "sol_optimizer_spp_instance.$extension"
    if (-not (Test-Path -LiteralPath $generated -PathType Leaf)) {
        throw "Pinned LBF did not generate the expected .$extension fixture"
    }
    $content = [IO.File]::ReadAllText($generated)
    $content = $content.Replace("`r`n", "`n").Replace("`r", "`n").Replace("`n", "`r`n")
    [IO.File]::WriteAllText(
        (Join-Path $fixtureOutput "sol_optimizer_spp_instance.$extension"),
        $content,
        [System.Text.UTF8Encoding]::new($false))
}

$bppSolutionFolder = Join-Path $solutionFolder 'bpp'
New-Item -ItemType Directory -Path $bppSolutionFolder | Out-Null
& rustup run 1.98.1 cargo run --locked --manifest-path (Join-Path $pinnedRoot 'lbf\Cargo.toml') -- `
    --input-file $bppInputFile `
    --solution-folder $bppSolutionFolder `
    --config-file $configFile `
    --prob-var bpp `
    --log-level info
if ($LASTEXITCODE -ne 0) {
    throw 'Pinned LBF BPP fixture generation failed'
}

$bppGenerated = Join-Path $bppSolutionFolder 'sol_bpp_instance.json'
if (-not (Test-Path -LiteralPath $bppGenerated -PathType Leaf)) {
    throw 'Pinned LBF did not generate the BPP JSON fixture'
}
$bppContent = [IO.File]::ReadAllText($bppGenerated)
$bppContent = $bppContent.Replace("`r`n", "`n").Replace("`r", "`n").Replace("`n", "`r`n")
$bppFixtureOutput = Join-Path $fixtureRoot 'LbfBpp'
New-Item -ItemType Directory -Force -Path $bppFixtureOutput | Out-Null
[IO.File]::WriteAllText(
    (Join-Path $bppFixtureOutput 'sol_bpp_instance.json'),
    $bppContent,
    [System.Text.UTF8Encoding]::new($false))

Write-Host "Generated pinned LBF SPP and BPP fixtures from jagua-rs $revision with Rust 1.98.1"
