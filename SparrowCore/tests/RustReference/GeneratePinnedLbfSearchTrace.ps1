param(
    [ValidateSet('none', 'continuous', 'discrete', 'discrete-three')]
    [string]$TraceRotationMode = 'none',
    [ValidateSet('full', 'adjacent-x', 'invalid-bounds', 'tighten-invalid', 'no-valid-placement', 'no-valid-placement-rect', 'feasible-rect', 'feasible-obstacle-rect', 'quality-zone-filter', 'quality-zone-bpp-e2e', 'small-fixtures')]
    [string]$TraceBoundsMode = 'full',
    [ValidateRange(-1, 2147483647)]
    [int]$TraceSeed = -1,
    [ValidateRange(-1, 65535)]
    [int]$TraceSamples = -1,
    [double]$TraceObstacleBoxMinX = 0.0,
    [double]$TraceObstacleBoxMinY = 4.0,
    [ValidateSet('rect', 'triangle', 'concave', 'concave-offset', 'concave-rot90', 'concave-rot90-origin', 'concave-rot45', 'concave-rot45-searchpair', 'concave-rot225-searchpair', 'concave-notch-rot45-searchpair', 'concave-notch-rot225-searchpair')]
    [string]$TraceObstacleShape = 'rect',
    [ValidateSet('debug', 'release')]
    [string]$RustProfile = 'debug'
)

$ErrorActionPreference = 'Stop'

if (($TraceBoundsMode -eq 'quality-zone-filter' -or $TraceBoundsMode -eq 'quality-zone-bpp-e2e') -and $TraceRotationMode -ne 'none') {
    throw 'Quality-zone fixtures use Rotation=None'
}

if ($TraceSeed -ge 0 -and ($TraceBoundsMode -ne 'feasible-obstacle-rect' -or $TraceRotationMode -ne 'continuous')) {
    throw 'TraceSeed override is currently supported only for the continuous feasible-obstacle fixture'
}
if ($TraceSamples -eq 0) {
    throw 'TraceSamples must be positive when supplied'
}
if ($TraceSamples -ge 0 -and ($TraceBoundsMode -ne 'feasible-obstacle-rect' -or $TraceRotationMode -ne 'continuous')) {
    throw 'TraceSamples override is currently supported only for the continuous feasible-obstacle fixture'
}
$allowedNonRectSeed = $TraceSeed -eq 42 -or (($TraceObstacleShape -eq 'concave-offset' -or $TraceObstacleShape -eq 'concave-rot90') -and $TraceSeed -eq 17)
$discreteTriangleFixture = $TraceObstacleShape -eq 'triangle' -and $TraceBoundsMode -eq 'feasible-obstacle-rect' -and $TraceRotationMode -eq 'discrete-three' -and $TraceSeed -lt 0 -and $TraceSamples -lt 0
if ($TraceObstacleShape -ne 'rect' -and -not $discreteTriangleFixture -and ($TraceBoundsMode -ne 'feasible-obstacle-rect' -or $TraceRotationMode -ne 'continuous' -or -not $allowedNonRectSeed -or $TraceSamples -ne 96)) {
    throw 'Non-rectangular obstacle tracing is locked to the continuous 96-sample fixture; seed 17 is additionally allowed for concave-offset and concave-rot90'
}

$revision = 'f5e2b5ffd14931f17a20ce283a2122e877953ceb'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$oracleManifest = Join-Path $repoRoot 'JaguarCore\Tools\RustOracle\Cargo.toml'
$recordedLock = Join-Path $repoRoot 'JaguarCore\Tools\RustOracle\LbfFixture.Cargo.lock'
$helperSource = Join-Path $PSScriptRoot 'pinned_lbf_search_trace.rs'
$fixtureSuffix = switch ($TraceRotationMode) {
    'none' { '' }
    'continuous' { '-Continuous' }
    'discrete' { '-Discrete' }
    'discrete-three' { '-Discrete-0-45-90' }
}
$seedSuffix = if ($TraceSeed -ge 0) { "-Seed$TraceSeed" } else { '' }
$samplesSuffix = if ($TraceSamples -ge 0) { "-Samples$TraceSamples" } else { '' }
$obstacleSuffix = switch ($TraceObstacleShape) {
    'rect' { '' }
    'triangle' { '-Triangle' }
    'concave' { '-Concave' }
    'concave-offset' { '-ConcaveOffset' }
    'concave-rot90' { '-ConcaveRot90' }
    'concave-rot90-origin' { '-ConcaveRot90Origin' }
    'concave-rot45' { '-ConcaveRot45' }
    'concave-rot45-searchpair' { '-ConcaveRot45SearchPair' }
    'concave-rot225-searchpair' { '-ConcaveRot225SearchPair' }
    'concave-notch-rot45-searchpair' { '-ConcaveNotchRot45SearchPair' }
    'concave-notch-rot225-searchpair' { '-ConcaveNotchRot225SearchPair' }
}
$boundsSuffix = switch ($TraceBoundsMode) {
    'full' { '' }
    'adjacent-x' { 'AdjacentX' }
    'invalid-bounds' { 'InvalidBounds' }
    'tighten-invalid' { 'TightenInvalid' }
    'no-valid-placement' { 'NoValidPlacement' }
    'no-valid-placement-rect' { 'NoValidPlacementRect' }
    'feasible-rect' { 'FeasibleRect' }
    'feasible-obstacle-rect' { 'FeasibleObstacleRect' }
    'quality-zone-filter' { 'QualityZoneFilter' }
    'quality-zone-bpp-e2e' { 'QualityZoneBppE2E' }
    'small-fixtures' { 'SmallFixtures' }
}
$diagnosticRound = if ($TraceBoundsMode -eq 'small-fixtures') { 'l1-r15-small-fixtures' } elseif ($TraceBoundsMode -eq 'quality-zone-bpp-e2e') { 'l1-r14-quality-zone-bpp' } elseif ($TraceBoundsMode -eq 'quality-zone-filter') { 'l1-r13-quality-zones' } elseif ($TraceObstacleShape -eq 'concave-notch-rot45-searchpair' -or $TraceObstacleShape -eq 'concave-notch-rot225-searchpair') { 'l2-r19' } elseif ($TraceObstacleShape -eq 'concave-rot45-searchpair' -or $TraceObstacleShape -eq 'concave-rot225-searchpair') { 'l2-r18' } elseif ($TraceObstacleShape -eq 'concave-rot90-origin') { 'l2-r17' } elseif ($TraceObstacleShape -eq 'concave-rot45') { 'l2-r16' } elseif ($TraceObstacleShape -eq 'concave-rot90' -and $TraceSeed -eq 17) { 'l2-r15' } elseif ($TraceObstacleShape -eq 'concave-rot90') { 'l2-r14' } elseif ($TraceObstacleShape -eq 'concave-offset' -and $TraceSeed -eq 17) { 'l2-r13' } elseif ($TraceObstacleShape -eq 'concave-offset') { 'l2-r12' } elseif ($TraceObstacleShape -eq 'concave') { 'l2-r11' } elseif ($TraceObstacleShape -eq 'triangle' -and $TraceRotationMode -eq 'discrete-three') { 'l2-r39' } elseif ($TraceObstacleShape -eq 'triangle') { 'l2-r10' } elseif ($TraceBoundsMode -eq 'feasible-obstacle-rect' -and $TraceRotationMode -eq 'discrete-three') { 'l2-r38' } elseif ($TraceBoundsMode -eq 'feasible-obstacle-rect' -and $TraceRotationMode -eq 'discrete') { 'l2-r37' } elseif ($TraceSamples -ge 0) { 'l2-r9' } elseif ($TraceSeed -ge 0) { 'l2-r8' } elseif ($TraceBoundsMode -eq 'feasible-obstacle-rect' -and $TraceRotationMode -eq 'continuous') { 'l2-r7' } elseif ($TraceBoundsMode -eq 'feasible-obstacle-rect' -and $TraceRotationMode -eq 'none') { 'l2-r36' } elseif ($TraceBoundsMode -eq 'feasible-obstacle-rect') { 'l2-r6' } elseif ($TraceBoundsMode -eq 'feasible-rect' -and $TraceRotationMode -eq 'none') { 'l2-r36' } elseif ($TraceBoundsMode -eq 'feasible-rect') { 'l2-r5' } elseif ($TraceBoundsMode -eq 'no-valid-placement-rect') { 'l2-r4' } elseif ($TraceBoundsMode -eq 'no-valid-placement') { 'l2-r3' } else { 'l2-r2' }
$binarySuffix = [guid]::NewGuid().ToString('N').Substring(0, 8)
$binaryName = "jaguar_run_lbf_search_trace_${diagnosticRound}_${RustProfile}_$($TraceRotationMode)_$($TraceBoundsMode.Replace('-', '_'))_$binarySuffix"
$diagnosticDirectory = Join-Path $repoRoot "SparrowCore\build\$diagnosticRound"
$outputPath = if ($TraceBoundsMode -eq 'small-fixtures') {
    New-Item -ItemType Directory -Force -Path $diagnosticDirectory | Out-Null
    Join-Path $diagnosticDirectory "rust-$RustProfile.tsv"
} elseif ($RustProfile -eq 'release' -and $TraceBoundsMode -ne 'full') {
    New-Item -ItemType Directory -Force -Path $diagnosticDirectory | Out-Null
    Join-Path $diagnosticDirectory "rust-release-$boundsSuffix$fixtureSuffix$obstacleSuffix$seedSuffix$samplesSuffix.tsv"
} elseif ($TraceBoundsMode -eq 'full') {
    Join-Path $repoRoot "SparrowCore\Tests\Fixtures\LbfPinnedSearchRustTrace$fixtureSuffix.tsv"
} elseif ($TraceBoundsMode -eq 'no-valid-placement') {
    Join-Path $repoRoot 'SparrowCore\Tests\Fixtures\LbfSearchNoUniformPlacementRustTrace.tsv'
} elseif ($TraceBoundsMode -eq 'no-valid-placement-rect') {
    Join-Path $repoRoot 'SparrowCore\Tests\Fixtures\LbfSearchRectNoUniformPlacementRustTrace.tsv'
} elseif ($TraceBoundsMode -eq 'feasible-rect') {
    Join-Path $repoRoot 'SparrowCore\Tests\Fixtures\LbfSearchRectFeasibleRustTrace.tsv'
} elseif ($TraceBoundsMode -eq 'feasible-obstacle-rect') {
    Join-Path $repoRoot ('SparrowCore\Tests\Fixtures\LbfSearchRectObstacleFeasible' + $fixtureSuffix + $obstacleSuffix + $seedSuffix + $samplesSuffix + 'RustTrace.tsv')
} elseif ($TraceBoundsMode -eq 'quality-zone-filter') {
    Join-Path $repoRoot 'SparrowCore\Tests\Fixtures\LbfQualityZoneFilterRustTrace.tsv'
} elseif ($TraceBoundsMode -eq 'quality-zone-bpp-e2e') {
    Join-Path $repoRoot 'SparrowCore\Tests\Fixtures\LbfQualityZoneBppRustReference.tsv'
} else {
    Join-Path $repoRoot "SparrowCore\Tests\Fixtures\LbfSamplerBoundary${boundsSuffix}RustTrace.tsv"
}
$normalOutputPath = if ($TraceRotationMode -eq 'none' -and $TraceBoundsMode -eq 'full') {
    Join-Path $repoRoot 'SparrowCore\Tests\Fixtures\LbfPinnedNormalRustTrace.tsv'
} else {
    New-Item -ItemType Directory -Force -Path $diagnosticDirectory | Out-Null
    Join-Path $diagnosticDirectory "normal-$TraceRotationMode-$TraceBoundsMode$obstacleSuffix$seedSuffix$samplesSuffix.tsv"
}
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
$temporaryBinary = Join-Path $binaryDirectory "$binaryName.rs"
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
        $cargoArguments = @('run', '--locked', '--manifest-path', $manifest)
        if ($RustProfile -eq 'release') {
            $cargoArguments += '--release'
        }
        $cargoArguments += @(
            '--bin', $binaryName, '--', $outputPath, $normalOutputPath,
            $TraceRotationMode, $TraceBoundsMode)
        if ($TraceSeed -ge 0) {
            $cargoArguments += $TraceSeed.ToString()
        } elseif ($discreteTriangleFixture) {
            $cargoArguments += '17'
        }
        if ($TraceSamples -ge 0) {
            if ($TraceSeed -lt 0) {
                throw 'TraceSamples requires an explicit TraceSeed so the fixture is reproducible'
            }
            $cargoArguments += $TraceSamples.ToString()
        } elseif ($discreteTriangleFixture) {
            $cargoArguments += '80'
        }
        if ($TraceObstacleShape -ne 'rect') {
            $cargoArguments += $TraceObstacleShape
        }
        if (($TraceObstacleShape -eq 'concave-notch-rot45-searchpair') -or ($TraceObstacleShape -eq 'concave-notch-rot225-searchpair')) {
            $culture = [Globalization.CultureInfo]::InvariantCulture
            $cargoArguments += @(
                $TraceObstacleBoxMinX.ToString($culture),
                $TraceObstacleBoxMinY.ToString($culture))
        }
        & rustup run 1.98.1 cargo @cargoArguments
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
